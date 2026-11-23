#define _POSIX_C_SOURCE 200809L
#include "store.h"
#include "installed-artwork.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __SWITCH2__
#include <switch.h>
typedef Sha256Context Hash;
static bool hash_open(Hash *hash) { sha256ContextCreate(hash); return true; }
static bool hash_write(Hash *hash, const void *bytes, size_t size) {
    sha256ContextUpdate(hash, bytes, size);
    return true;
}
static bool hash_close(Hash *hash, uint8_t *digest) { sha256ContextGetHash(hash, digest); return true; }
#else
#include <openssl/evp.h>
typedef EVP_MD_CTX *Hash;
static bool hash_open(Hash *hash) {
    *hash = EVP_MD_CTX_new();
    if (*hash && EVP_DigestInit_ex(*hash, EVP_sha256(), NULL) == 1) return true;
    EVP_MD_CTX_free(*hash);
    return false;
}
static bool hash_write(Hash *hash, const void *bytes, size_t size) {
    return EVP_DigestUpdate(*hash, bytes, size) == 1;
}
static bool hash_close(Hash *hash, uint8_t *digest) {
    bool ok = EVP_DigestFinal_ex(*hash, digest, NULL) == 1;
    EVP_MD_CTX_free(*hash);
    return ok;
}
#endif

static const char signature[] = "IZSTORE1";
enum { BLOCK_SIZE = 128 * 1024 };
enum { ICON_BYTES = STORE_ICON_SIZE * STORE_ICON_SIZE * 4, ICON_ID_BYTES = 48 };
enum { LOCAL_CATALOG = STORE_MAX_CATALOGS };
typedef enum { ABSENT, REGULAR, OTHER } FileState;

#ifdef IZ_STORE_STANDALONE
#define INSTALL_DIR "/switch"
#else
#define INSTALL_DIR MODULE_DIR
#endif

static bool path(char out[STORE_PATH], const char *pattern, ...) {
    va_list args;
    va_start(args, pattern);
    int size = vsnprintf(out, STORE_PATH, pattern, args);
    va_end(args);
    return size >= 0 && size < STORE_PATH;
}

static bool message(char out[STORE_MESSAGE], const char *text) {
    snprintf(out, STORE_MESSAGE, "%s", text);
    return false;
}

static FileState file_state(const char *name) {
    struct stat st;
    if (lstat(name, &st) == 0) return S_ISREG(st.st_mode) ? REGULAR : OTHER;
    return errno == ENOENT ? ABSENT : OTHER;
}

static bool folder(const char *name) {
    struct stat st;
    if (mkdir(name, 0755) == 0) return true;
    return errno == EEXIST && lstat(name, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool commit(const char *directory) {
#ifdef __SWITCH2__
    (void)directory;
    return R_SUCCEEDED(fsdevCommitDevice("sdmc"));
#else
    int fd = open(directory, O_RDONLY);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    return close(fd) == 0 && ok;
#endif
}

static bool flush(FILE *file) { return fflush(file) == 0 && fsync(fileno(file)) == 0; }


const char *store_extension(PackageKind kind) { return kind == PACKAGE_IZM ? "iza" : "nro"; }

static const char *kind_field(PackageKind kind) {
    if (kind == PACKAGE_NRO) return "nro";
    return kind == PACKAGE_IZM_NRO ? "izm-nro" : "izm";
}

static bool identifier(const char *id) {
    size_t size = strlen(id);
    if (!size || size >= 48 || *id == '-') return false;
    for (; *id; ++id)
        if (!((*id >= 'a' && *id <= 'z') || (*id >= '0' && *id <= '9') || *id == '-')) return false;
    return true;
}

static bool hexadecimal(const char *value) {
    return strlen(value) == 64 && strspn(value, "0123456789abcdef") == 64;
}

static bool field(char *out, size_t capacity, const char *value) {
    size_t size = strlen(value);
    if (!size || size >= capacity) return false;
    for (const unsigned char *ch = (const unsigned char *)value; *ch; ++ch)
        if (*ch < 32 || *ch == 127) return false;
    memcpy(out, value, size + 1);
    return true;
}

static bool repository_url(const char *url) {
    if (strncmp(url, "https://", 8) || strlen(url) >= RELEASE_URL_MAX) return false;
    const char *host = url + 8, *slash = strchr(host, '/');
    if (!slash || slash == host || !slash[1]) return false;
    for (const unsigned char *p = (const unsigned char *)host; *p; ++p)
        if (*p <= 32 || *p >= 127 || *p == '\\') return false;
    return !memchr(host, '@', (size_t)(slash - host));
}

void store_repository(const Package *package, char url[RELEASE_URL_MAX]) {
    snprintf(url, RELEASE_URL_MAX, "%s", package->repository);
    if (!url[0] && package->release_url[0]) {
        snprintf(url, RELEASE_URL_MAX, "%s", package->release_url);
        char *release = strstr(url, "/releases/");
        if (release) *release = 0;
    }
}

static bool parts(char *line, char *values[], unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
        values[index] = line;
        char *tab = strchr(line, '\t');
        if (index + 1 == count) return !tab;
        if (!tab) return false;
        *tab = '\0';
        line = tab + 1;
    }
    return false;
}

static bool app_line(Package *app, char *line) {
    char *values[7];
    if (!parts(line, values, 7) || !identifier(values[0])) return false;
    if (!strcmp(values[1], "nro")) app->kind = PACKAGE_NRO;
    else if (!strcmp(values[1], "izm-nro")) app->kind = PACKAGE_IZM_NRO;
    else if (!strcmp(values[1], "iza") || !strcmp(values[1], "izm")) app->kind = PACKAGE_IZM;
    else return false;
    return field(app->id, sizeof app->id, values[0]) &&
           field(app->version, sizeof app->version, values[2]) &&
           field(app->title, sizeof app->title, values[3]) &&
           field(app->author, sizeof app->author, values[4]) &&
           field(app->category, sizeof app->category, values[5]) &&
           field(app->description, sizeof app->description, values[6]);
}

static bool file_line(Package *app, char *line, bool receipt) {
    char *values[4];
    bool executable = app->count == 0;
    if (app->count >= STORE_MAX_FILES || (!executable && app->kind != PACKAGE_IZM) ||
        !parts(line, values, receipt ? 4 : 3) || !hexadecimal(values[1])) return false;
    char expected[80];
    if (executable) snprintf(expected, sizeof expected, "launcher.%s", store_extension(app->kind));
    else snprintf(expected, sizeof expected, "asset-%s.izdata", values[1]);
    if (strcmp(values[2], expected)) return false;
    for (unsigned i = 0; i < app->count; ++i)
        if (!strcmp(app->files[i].name, expected)) return false;
    size_t digits = strspn(values[0], "0123456789");
    if (!digits || digits > 9 || values[0][digits]) return false;
    uint64_t bytes = strtoull(values[0], NULL, 10);
    if (!bytes || bytes > 128u * 1024u * 1024u) return false;
    if (receipt && strcmp(values[3], "0") && strcmp(values[3], "1")) return false;
    if (receipt && *values[3] == '1') app->owned |= UINT32_C(1) << app->count;
    PackageFile *file = &app->files[app->count++];
    strcpy(file->name, expected);
    strcpy(file->digest, values[1]);
    file->bytes = bytes;
    return true;
}

static bool trim_line(char line[768]) {
    char *end = strchr(line, '\n');
    if (!end) return false;
    if (end != line && end[-1] == '\r') --end;
    *end = '\0';
    return true;
}

static bool collect_catalogs(Store *store, StoreSettings *settings, bool download,
                             CatalogRefreshMode mode, char out[STORE_MESSAGE]);

static bool preferred_kind(PackageKind kind) {
#ifdef IZ_STORE_STANDALONE
    return kind == PACKAGE_NRO;
#else
    return kind == PACKAGE_IZM;
#endif
}

static bool parse(FILE *file, App *apps, unsigned limit, unsigned *count, bool receipt) {
    char line[768];
    *count = 0;
    if (!fgets(line, sizeof line, file) || !trim_line(line)) return false;
    bool variants = !receipt && !strcmp(line, "IZSTORE3");
    bool online_catalog = variants || (!receipt && !strcmp(line, "IZSTORE2"));
    if (!online_catalog && strcmp(line, signature)) return false;
    while (fgets(line, sizeof line, file)) {
        if (!trim_line(line)) return false;
        if (!strncmp(line, "app\t", 4)) {
            if (*count == limit || (*count && !apps[*count - 1].package.count)) return false;
            App *app = &apps[*count];
            memset(app, 0, sizeof *app);
            app->icon = (int)*count;
            if (!app_line(&app->package, line + 4)) return false;
            for (unsigned i = 0; i < *count; ++i)
                if (!strcmp(apps[i].package.id, app->package.id) &&
                    (!variants || apps[i].package.kind == app->package.kind ||
                     apps[i].package.kind == PACKAGE_IZM_NRO || app->package.kind == PACKAGE_IZM_NRO)) return false;
            ++*count;
        } else {
            if (!*count) return false;
            Package *package = &apps[*count - 1].package;
            if (online_catalog && !strncmp(line, "repository\t", 11)) {
                if (package->repository[0] || !repository_url(line + 11)) return false;
                strcpy(package->repository, line + 11);
            } else if (online_catalog && !strncmp(line, "github\t", 7)) {
                if (package->kind == PACKAGE_IZM_NRO || !package->count || package->release_url[0] ||
                    !github_release_url(line + 7, store_extension(package->kind))) return false;
                strcpy(package->release_url, line + 7);
            } else if (strncmp(line, "file\t", 5) || !file_line(package, line + 5, receipt)) return false;
        }
    }
    if (!feof(file) || ferror(file) || !*count || !apps[*count - 1].package.count ||
        (receipt && !(apps[0].package.owned & 1u))) return false;
    if (variants) {
        unsigned selected = 0;
        for (unsigned i = 0; i < *count; ++i) {
            unsigned slot = 0;
            while (slot < selected && strcmp(apps[slot].package.id, apps[i].package.id)) ++slot;
            if (slot == selected || preferred_kind(apps[i].package.kind)) apps[slot] = apps[i];
            if (slot == selected) ++selected;
        }
        *count = selected;
        for (unsigned i = 0; i < selected; ++i) apps[i].icon = (int)i;
    }
    return true;
}

void store_clear(Store *store) {
    free(store->apps); store->apps = NULL; store->count = 0;
    store->catalog_count = 0;
    store->update_checked = false;
    store->local_catalog_available = false;
}

bool store_clone(Store *target, const Store *source) {
    if (target == source) return true;
    App *copy = source->count ? malloc((size_t)source->count * sizeof *copy) : NULL;
    if (source->count && !copy) return false;
    if (source->count) memcpy(copy, source->apps, (size_t)source->count * sizeof *copy);
    store_clear(target); *target = *source; target->apps = copy;
    return true;
}

/* Count validated record boundaries before allocating. No app-count ceiling. */
static bool parse_list(FILE *file, App **apps, unsigned *count) {
    long start = ftell(file);
    char line[768]; size_t capacity = 0;
    if (start < 0) return false;
    while (fgets(line, sizeof line, file)) {
        if (!trim_line(line)) return false;
        if (!strncmp(line, "app\t", 4)) {
            if (capacity == UINT_MAX || capacity >= SIZE_MAX / sizeof(App)) return false;
            ++capacity;
        }
    }
    if (ferror(file) || !capacity || fseek(file, start, SEEK_SET)) return false;
    App *next = calloc(capacity, sizeof *next);
    if (!next) return false;
    unsigned parsed = 0;
    if (!parse(file, next, (unsigned)capacity, &parsed, false)) { free(next); return false; }
    free(*apps); *apps = next; *count = parsed;
    return true;
}

static bool load_local_list(Store *store, const char *source, char out[STORE_MESSAGE]) {
    store->count = 0;
    FileState state = file_state(source);
    if (state == ABSENT) return true;
    FILE *file = state == REGULAR ? fopen(source, "rb") : NULL;
    if (!file) return message(out, "The local catalog could not be read.");
    bool ok = parse_list(file, &store->apps, &store->count);
    fclose(file);
    if (ok) return true;
    store->count = 0;
    return message(out, "The local catalog is damaged or unsupported.");
}

static const char *local_catalog_dir(const char *root) {
    char name[STORE_PATH];
    if (path(name, "%s" STORE_CATALOG_DIR "/catalog.tsv", root) && file_state(name) != ABSENT)
        return STORE_CATALOG_DIR;
    return STORE_DIR;
}

static bool local_catalog_available(const char *root) {
    char name[STORE_PATH];
    return path(name, "%s%s/catalog.tsv", root, local_catalog_dir(root)) && file_state(name) == REGULAR;
}

static bool load_catalog(Store *store, const char *root, StoreSettings *settings,
                         bool download, CatalogRefreshMode mode, char out[STORE_MESSAGE]) {
    store_clear(store);
    if (!path(store->root, "%s", root) || strlen(root) + 256 >= STORE_PATH)
        return message(out, "SD path is too long.");
    return collect_catalogs(store, settings, download, mode, out);
}

bool store_load(Store *store, const char *root, char out[STORE_MESSAGE]) {
    StoreSettings settings;
    store_settings_read(root, &settings);
    return load_catalog(store, root, &settings, false, CATALOG_CACHED, out);
}

static bool cancelled(Progress progress) {
    return progress.cancel && atomic_load(progress.cancel);
}

static bool transfer(const char *source, FILE *destination, uint64_t bytes, char digest[65],
                     Progress progress, uint64_t done, uint64_t total) {
    if (file_state(source) != REGULAR || bytes > 128u * 1024u * 1024u) return false;
    FILE *input = fopen(source, "rb");
    if (!input) return false;
    struct stat st;
    uint8_t *block = malloc(BLOCK_SIZE);
    Hash hash;
    bool ok = block && fstat(fileno(input), &st) == 0 &&
              st.st_size > 0 && (uint64_t)st.st_size == bytes && hash_open(&hash);
    if (ok) {
        for (uint64_t offset = 0; offset < bytes;) {
            if (cancelled(progress)) { ok = false; break; }
            size_t count = bytes - offset < BLOCK_SIZE ? (size_t)(bytes - offset) : BLOCK_SIZE;
            if (fread(block, 1, count, input) != count || !hash_write(&hash, block, count) ||
                (destination && fwrite(block, 1, count, destination) != count)) {
                ok = false;
                break;
            }
            offset += count;
            if (progress.report) progress.report(progress.context, done + offset, total);
        }
        if (fgetc(input) != EOF || ferror(input)) ok = false;
        uint8_t binary[32];
        if (!hash_close(&hash, binary)) ok = false;
        if (ok) for (unsigned index = 0; index < 32; ++index)
            snprintf(digest + 2 * index, 3, "%02x", binary[index]);
    }
    free(block);
    return fclose(input) == 0 && ok;
}

static bool matches(const char *name, const PackageFile *file) {
    char digest[65];
    return transfer(name, NULL, file->bytes, digest, (Progress){0}, 0, 0) &&
           !strcmp(digest, file->digest);
}

static uint32_t read_word(const uint8_t *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
        ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int compare_asset_names(const void *left, const void *right) {
    const PackageFile *const *a = left;
    const PackageFile *const *b = right;
    return strcmp((*a)->name, (*b)->name);
}

static bool valid_companions(const uint8_t header[512], const Package *app) {
    unsigned count = app->count - 1;
    if (!(read_word(header + 20) & 8u)) return count == 0;
    if (read_word(header + 8) != 2 || !count || read_word(header + 160) != count) return false;

    const PackageFile *files[STORE_MAX_FILES - 1];
    uint64_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        files[i] = &app->files[i + 1];
        total += files[i]->bytes;
    }
    if (read_word(header + 164) != total) return false;
    qsort(files, count, sizeof *files, compare_asset_names);

    // Canonical asset list used by the IZ2S package builder and importer.
    const char prefix[] = "iz2s.sd-assets.v1\n[";
    uint8_t digest[32];
    Hash hash;
    if (!hash_open(&hash)) return false;
    bool ok = hash_write(&hash, prefix, sizeof prefix - 1);
    for (unsigned i = 0; ok && i < count; ++i) {
        const PackageFile *asset = files[i];
        char row[192];
        int length = snprintf(row, sizeof row, "%s[\"%s\",%llu,\"%s\"]",
                              i ? "," : "", asset->name, (unsigned long long)asset->bytes, asset->digest);
        ok = length > 0 && (size_t)length < sizeof row && hash_write(&hash, row, (size_t)length);
    }
    ok = ok && hash_write(&hash, "]", 1);
    return hash_close(&hash, digest) && ok && !memcmp(header + 368, digest, sizeof digest);
}

static bool valid_image(const char *file, const Package *app) {
    uint8_t head[4096];
    size_t need = app->kind == PACKAGE_IZM ? 512 : sizeof head;
    if (app->files[0].bytes < need || file_state(file) != REGULAR) return false;
    FILE *input = fopen(file, "rb");
    if (!input) return false;
    bool ok = fread(head, 1, need, input) == need;
    fclose(input);
    if (!ok) return false;
    if (app->kind != PACKAGE_IZM) {
        if (memcmp(head + 16, "NRO0", 4)) return false;
#ifdef __SWITCH2__
        NroOunceInfo info;
        return nroGetOunceInfo(head, need, &info) && info.platform == NroPlatform_Switch2;
#else
        uint32_t mod = read_word(head + 4);
        if (mod < 128 || mod > need - 28 || memcmp(head + mod, "MOD0", 4)) return false;
        for (unsigned offset = mod + 28; offset + 12 <= need && offset + 12 <= mod + 128; offset += 4)
            if (!memcmp(head + offset, "OUN0", 4) && read_word(head + offset + 4) == 2 &&
                read_word(head + offset + 8) == 1) return true;
        return false;
#endif
    }
    uint32_t version = read_word(head + 8);
    return !memcmp(head, "IZSDAPP1", 8) && (version == 1 || version == 2) &&
        read_word(head + 12) == 512 && read_word(head + 16) == app->files[0].bytes &&
        !memcmp(head + 24, app->id, strlen(app->id) + 1) &&
        valid_companions(head, app);
}

static bool read_receipt(const char *directory, const Package *wanted, const char *file, App *receipt) {
    struct stat st;
    char digest[65], name[STORE_PATH];
    if (stat(file, &st) || st.st_size <= 0 ||
        !transfer(file, NULL, (uint64_t)st.st_size, digest, (Progress){0}, 0, 0) ||
        !path(name, "%s/.izstore-%s.tsv", directory, digest) || file_state(name) != REGULAR) return false;
    FILE *stream = fopen(name, "rb");
    if (!stream) return false;
    unsigned count;
    bool ok = parse(stream, receipt, 1, &count, true);
    fclose(stream);
    const Package *old = &receipt->package;
    return ok && count == 1 && !strcmp(old->id, wanted->id) &&
           !strcmp(store_extension(old->kind), store_extension(wanted->kind)) &&
           !strcmp(old->files[0].digest, digest) && old->files[0].bytes == (uint64_t)st.st_size;
}

static void module_paths(const Store *store, const Package *app, char dir[STORE_PATH],
                         char active[STORE_PATH], char backup[STORE_PATH]) {
    path(dir, "%s" INSTALL_DIR "/%s", store->root, app->id);
#ifdef IZ_STORE_STANDALONE
    path(active, "%s/%s.nro", dir, app->id);
    path(backup, "%s/.izstore-backup", dir);
#else
    path(active, "%s/%s", dir, app->files[0].name);
    path(backup, "%s/.izstore-backup.%s", dir, store_extension(app->kind));
#endif
}

static void cleanup_old(const char *directory, const Package *previous, const Package *next);

static bool recover(const char *directory, const char *active, const char *backup,
                    const Package *package) {
    if (file_state(backup) == ABSENT) return true;
    App old;
    if (!read_receipt(directory, package, backup, &old)) return false;
    if (file_state(active) == ABSENT)
        return rename(backup, active) == 0 && commit(directory);
    App current;
    if (!read_receipt(directory, package, active, &current) || unlink(backup) || !commit(directory)) return false;
    cleanup_old(directory, &old.package, &current.package);
    return commit(directory);
}

#ifdef IZ_STORE_STANDALONE
static FileState flat_install(const Store *store, const Package *app) {
    char name[STORE_PATH];
    path(name, "%s" INSTALL_DIR "/%s.nro", store->root, app->id);
    return file_state(name);
}
#else
static bool conflict(const char *directory, const Package *app) {
    char other[STORE_PATH];
    path(other, "%s/launcher.%s", directory, app->kind == PACKAGE_IZM ? "nro" : "iza");
    return file_state(other) != ABSENT;
}
#endif

static bool is_store_entry(const Package *package) {
    return !strcmp(package->id, "iz2s-cheese");
}

const App *store_self_entry(const Store *store) {
    if (store->update_checked) return &store->update;
    for (unsigned i = 0; i < store->count; ++i)
        if (is_store_entry(&store->apps[i].package) &&
#ifdef IZ_STORE_STANDALONE
            store->apps[i].package.kind == PACKAGE_NRO)
#else
            store->apps[i].package.kind == PACKAGE_IZM)
#endif
            return &store->apps[i];
    return NULL;
}

static const char *running_version;
#ifdef IZ_STORE_STANDALONE
static const char store_program[] = "/switch/iz2s-cheese/iz2s-cheese.nro";
void store_set_running_nro(const char *path, const char *version) {
    if (path && !strncmp(path, "sdmc:", 5)) path += 5;
    running_version = path && !strcmp(path, store_program) ? version : NULL;
}
#else
static const char store_program[] = MODULE_DIR "/iz2s-cheese/launcher.iza";
void store_set_running_iza(const char *version) { running_version = version; }
#endif

static bool stable_version(const char *value, unsigned parts[3]) {
    for (unsigned i = 0; i < 3; ++i) {
        size_t digits = strspn(value, "0123456789");
        if (!digits || digits > 4 || value[digits] != (i == 2 ? '\0' : '.')) return false;
        parts[i] = (unsigned)strtoul(value, NULL, 10);
        value += digits + (i < 2);
    }
    return true;
}

static bool self_update_available(const Store *store, const Package *package) {
    unsigned current[3], candidate[3];
#ifdef IZ_STORE_STANDALONE
    if (package->kind != PACKAGE_NRO || package->count != 1) return false;
#else
    if (package->kind != PACKAGE_IZM || package->count < 2) return false;
#endif
    if (!running_version || !stable_version(running_version, current) || !stable_version(package->version, candidate)) return false;
    char expected[RELEASE_URL_MAX];
    int length = snprintf(expected, sizeof expected,
        "https://github.com/byizmysh/iz2s-cheese/releases/download/v%s/iz2s-cheese.%s",
        package->version, store_extension(package->kind));
    if (length < 0 || (size_t)length >= sizeof expected || strcmp(expected, package->release_url)) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (candidate[i] != current[i]) return candidate[i] > current[i];
    char installed[STORE_PATH];
    return path(installed, "%s%s", store->root, store_program) && file_state(installed) == REGULAR &&
        !matches(installed, &package->files[0]);
}

void store_refresh(const Store *store, App *app) {
    app->installed_version[0] = '\0';
    if (is_store_entry(&app->package)) {
        app->state = SELF_ENTRY;
        if (running_version)
            snprintf(app->installed_version, sizeof app->installed_version, "%s", running_version);
        if (self_update_available(store, &app->package)) app->state = UPDATE_AVAILABLE;
        return;
    }
#ifdef IZ_STORE_STANDALONE
    if (app->package.kind == PACKAGE_IZM) { app->state = UNAVAILABLE; return; }
#endif
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH];
    module_paths(store, &app->package, directory, active, backup);
    if (!recover(directory, active, backup, &app->package)) { app->state = UNAVAILABLE; return; }
    FileState state = file_state(active);
#ifdef IZ_STORE_STANDALONE
    if (state == ABSENT) {
        FileState flat = flat_install(store, &app->package);
        app->state = flat == REGULAR ? EXTERNAL : flat == ABSENT ? NOT_INSTALLED : UNAVAILABLE;
        return;
    }
    if (state != REGULAR) { app->state = UNAVAILABLE; return; }
#else
    if (state == ABSENT) {
        char icon[STORE_PATH];
        if (path(icon, "%s/.izcheese-icon.rgba", directory) && file_state(icon) == REGULAR && unlink(icon) == 0)
            commit(directory);
        app->state = conflict(directory, &app->package) ? UNAVAILABLE : NOT_INSTALLED;
        return;
    }
    if (state != REGULAR || conflict(directory, &app->package)) { app->state = UNAVAILABLE; return; }
#endif
    App receipt;
    if (!read_receipt(directory, &app->package, active, &receipt)) { app->state = EXTERNAL; return; }
    strcpy(app->installed_version, receipt.package.version);
    app->state = strcmp(receipt.package.files[0].digest, app->package.files[0].digest)
        ? UPDATE_AVAILABLE : INSTALLED;
    for (unsigned i = 1; i < receipt.package.count; ++i) {
        char resource[STORE_PATH];
        path(resource, "%s/%s", directory, receipt.package.files[i].name);
        if (file_state(resource) != REGULAR) app->state = UPDATE_AVAILABLE;
    }
}

/* Bounded JSON reader for GitHub's official release metadata. Never evaluates JSON. */
typedef struct { const char *begin, *end; } Json;
static const char *json_space(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

static const char *json_end(const char *p, const char *end, unsigned depth) {
    p = json_space(p, end);
    if (p == end || depth > 24) return NULL;
    if (*p == '"') {
        for (++p; p < end; ++p) {
            if ((unsigned char)*p < 32) return NULL;
            if (*p == '"') return p + 1;
            if (*p == '\\' && ++p == end) return NULL;
        }
        return NULL;
    }
    if (*p == '{' || *p == '[') {
        bool object = *p == '{'; char close = object ? '}' : ']';
        p = json_space(p + 1, end);
        if (p < end && *p == close) return p + 1;
        while (p < end) {
            if (object) {
                if (*p != '"' || !(p = json_end(p, end, depth + 1))) return NULL;
                p = json_space(p, end);
                if (p == end || *p != ':') return NULL;
                p = json_space(p + 1, end);
            }
            if (!(p = json_end(p, end, depth + 1))) return NULL;
            p = json_space(p, end);
            if (p < end && *p == close) return p + 1;
            if (p == end || *p != ',') return NULL;
            p = json_space(p + 1, end);
        }
        return NULL;
    }
    const char *start = p;
    while (p < end && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') ++p;
    return p == start ? NULL : p;
}

static Json json_member(Json object, const char *key) {
    const char *p = json_space(object.begin, object.end);
    if (p == object.end || *p++ != '{') return (Json){0};
    p = json_space(p, object.end);
    while (p < object.end && *p != '}') {
        const char *key_end = json_end(p, object.end, 0);
        if (!key_end || *p != '"') break;
        bool match = (size_t)(key_end - p) == strlen(key) + 2 && !memcmp(p + 1, key, strlen(key));
        p = json_space(key_end, object.end);
        if (p == object.end || *p++ != ':') break;
        p = json_space(p, object.end);
        const char *end = json_end(p, object.end, 0);
        if (!end) break;
        if (match) return (Json){p, end};
        p = json_space(end, object.end);
        if (p == object.end || *p++ != ',') break;
        p = json_space(p, object.end);
    }
    return (Json){0};
}

static bool json_text(Json value, char *out, size_t capacity) {
    if (!value.begin || value.end - value.begin < 2 || *value.begin != '"' || value.end[-1] != '"') return false;
    size_t bytes = (size_t)(value.end - value.begin - 2);
    if (bytes >= capacity) return false;
    /* Selected fields are ASCII names, hashes and URLs; no escaped forms are accepted. */
    for (size_t i = 0; i < bytes; ++i)
        if ((unsigned char)value.begin[i + 1] < 32 || value.begin[i + 1] == '\\') return false;
    memcpy(out, value.begin + 1, bytes); out[bytes] = 0; return true;
}

typedef struct { char *data; size_t bytes; } ReleaseBuffer;
static bool release_sink(void *context, const void *bytes, size_t count) {
    ReleaseBuffer *buffer = context;
    if (count > 64u * 1024u - buffer->bytes) return false;
    memcpy(buffer->data + buffer->bytes, bytes, count); buffer->bytes += count;
    return true;
}

static bool release_package(Json release, Package *package) {
    char tag[32]; unsigned version[3];
    Json draft = json_member(release, "draft"), pre = json_member(release, "prerelease");
    if (!draft.begin || draft.end - draft.begin != 5 || memcmp(draft.begin, "false", 5) ||
        !pre.begin || pre.end - pre.begin != 5 || memcmp(pre.begin, "false", 5) ||
        !json_text(json_member(release, "tag_name"), tag, sizeof tag) || *tag != 'v' ||
        !stable_version(tag + 1, version)) return false;
#ifdef IZ_STORE_STANDALONE
    package->kind = PACKAGE_NRO;
#else
    package->kind = PACKAGE_IZM;
#endif
    strcpy(package->id, "iz2s-cheese"); strcpy(package->title, "IZ2S Cheese");
    strcpy(package->author, "IZMYSH"); strcpy(package->category, "Tools");
    strcpy(package->version, tag + 1);
    strcpy(package->repository, "https://github.com/byizmysh/iz2s-cheese");
    char launcher[32]; snprintf(launcher, sizeof launcher, "iz2s-cheese.%s", store_extension(package->kind));
    Json assets = json_member(release, "assets");
    if (!assets.begin || *assets.begin != '[') return false;
    const char *p = json_space(assets.begin + 1, assets.end);
    bool executable = false;
    while (p < assets.end && *p != ']') {
        const char *end = json_end(p, assets.end, 0);
        if (!end || *p != '{') return false;
        Json asset = {p, end}; char name[128], digest[80], url[RELEASE_URL_MAX];
        if (!json_text(json_member(asset, "name"), name, sizeof name)) return false;
        bool main = !strcmp(name, launcher);
        bool companion = package->kind == PACKAGE_IZM && !strncmp(name, "asset-", 6) &&
            strlen(name) == 77 && !strcmp(name + 70, ".izdata");
        if (main || companion) {
            Json size = json_member(asset, "size");
            if (!size.begin || size.end - size.begin > 9 || size.begin == size.end ||
                !json_text(json_member(asset, "digest"), digest, sizeof digest) ||
                strncmp(digest, "sha256:", 7) || !hexadecimal(digest + 7) ||
                !json_text(json_member(asset, "browser_download_url"), url, sizeof url)) return false;
            for (const char *digit = size.begin; digit < size.end; ++digit)
                if (*digit < '0' || *digit > '9') return false;
            uint64_t bytes = strtoull(size.begin, NULL, 10);
            if (!bytes || bytes > 128u * 1024u * 1024u || package->count == STORE_MAX_FILES) return false;
            char expected[RELEASE_URL_MAX];
            int length = snprintf(expected, sizeof expected,
                "https://github.com/byizmysh/iz2s-cheese/releases/download/%s/%s", tag, name);
            if (length < 0 || length >= (int)sizeof expected || strcmp(expected, url) ||
                !github_release_url(url, NULL)) return false;
            if ((main && executable) || (companion && strncmp(name + 6, digest + 7, 64))) return false;
            for (unsigned i = 0; i < package->count; ++i)
                if (!strcmp(package->files[i].name, name)) return false;
            unsigned slot = package->count++;
            if (main) {
                if (slot) package->files[slot] = package->files[0];
                slot = 0; executable = true; strcpy(package->release_url, url);
            }
            PackageFile *file = &package->files[slot];
            file->bytes = bytes; strcpy(file->digest, digest + 7);
            if (main) snprintf(file->name, sizeof file->name, "launcher.%s", store_extension(package->kind));
            else memcpy(file->name, name, strlen(name) + 1);
        }
        p = json_space(end, assets.end);
        if (p < assets.end && *p == ',') p = json_space(p + 1, assets.end);
        else break;
    }
    return executable;
}

bool store_check_update(Store *store, char out[STORE_MESSAGE]) {
    store->update_checked = false;
    ReleaseBuffer buffer = {.data = malloc(64u * 1024u + 1)};
    if (!buffer.data) return message(out, "Not enough memory to check updates.");
    bool ok = github_latest_release(release_sink, &buffer, out);
    App update = {0};
    if (ok) {
        const char *end = json_end(buffer.data, buffer.data + buffer.bytes, 0);
        ok = end && json_space(end, buffer.data + buffer.bytes) == buffer.data + buffer.bytes &&
            release_package((Json){buffer.data, end}, &update.package);
        if (!ok) message(out, "The official release has incomplete or unsupported update metadata.");
    }
    free(buffer.data);
    if (!ok) return false;
    store_refresh(store, &update);
    store->update = update; store->update_checked = true;
    if (update.state == UPDATE_AVAILABLE)
        snprintf(out, STORE_MESSAGE, "IZ2S Cheese %s is available. Download and install this update?", update.package.version);
    else if (!running_version)
        snprintf(out, STORE_MESSAGE, "Latest version: %s. Use the official SD path to enable updates.", update.package.version);
    else
        snprintf(out, STORE_MESSAGE, "You are up to date. Running %s; latest stable release %s.", running_version, update.package.version);
    return true;
}

static FILE *open_new(const char *name) {
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, "wb");
    if (!file) { close(fd); unlink(name); }
    return file;
}

static FILE *control_read(const char *name) {
    char backup[STORE_PATH];
    if (file_state(name) == REGULAR) return fopen(name, "rb");
    if (file_state(name) == ABSENT && path(backup, "%s.bak", name) && file_state(backup) == REGULAR)
        return fopen(backup, "rb");
    return NULL;
}

static bool read_program_icon(const char *active, bool iza, uint32_t *pixels) {
    if (file_state(active) != REGULAR) return false;
    FILE *file = fopen(active, "rb");
    if (!file) return false;
    struct stat info;
    bool ok = !fstat(fileno(file), &info) && info.st_size > 0 &&
        installed_artwork(file, (uint64_t)info.st_size, iza, pixels);
    fclose(file);
    return ok;
}

bool store_read_installed_icon(const Store *store, const App *app, uint32_t *pixels) {
    if (app->state != INSTALLED && app->state != UPDATE_AVAILABLE && app->state != EXTERNAL)
        return false;
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH], sidecar[STORE_PATH];
    module_paths(store, &app->package, directory, active, backup);
    if (path(sidecar, "%s/.izcheese-icon.rgba", directory)) {
        FILE *file = control_read(sidecar);
        if (file) {
            bool ok = fread(pixels, 1, ICON_BYTES, file) == ICON_BYTES && fgetc(file) == EOF && !ferror(file);
            fclose(file);
            if (ok) return true;
        }
    }
#ifdef IZ_STORE_STANDALONE
    if (file_state(active) == ABSENT &&
        !path(active, "%s" INSTALL_DIR "/%s.nro", store->root, app->package.id)) return false;
#endif
    return read_program_icon(active, app->package.kind == PACKAGE_IZM, pixels);
}

static bool control_publish(const char *stage, const char *final, const char *directory) {
    char backup[STORE_PATH];
    if (!path(backup, "%s.bak", final) || file_state(backup) == OTHER || file_state(final) == OTHER)
        return false;
    bool existing = file_state(final) == REGULAR;
    if (existing && file_state(backup) == REGULAR && unlink(backup)) return false;
    if (existing && rename(final, backup)) return false;
    if (rename(stage, final)) {
        if (existing) rename(backup, final);
        return false;
    }
    if (!commit(directory)) return false;
    if (file_state(backup) == REGULAR) unlink(backup);
    return commit(directory);
}

void store_sync_icon_pixels(const Store *store, const App *app, const void *rgba) {
#ifdef IZ_STORE_STANDALONE
    // hbmenu reads the executable's embedded icon.
    (void)store;
    (void)app;
    (void)rgba;
#else
    if ((app->state != INSTALLED && app->state != UPDATE_AVAILABLE) || app->package.kind == PACKAGE_IZM) return;
    const unsigned char *pixels = rgba;
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH], to[STORE_PATH], stage[STORE_PATH];
    module_paths(store, &app->package, directory, active, backup);
    if (!path(to, "%s/.izcheese-icon.rgba", directory) ||
        !path(stage, "%s/.izcheese-icon.tmp", directory)) return;
    if (file_state(to) == REGULAR) {
        FILE *old = fopen(to, "rb");
        if (old) {
            unsigned char block[1024];
            bool same = true;
            for (size_t at = 0; at < ICON_BYTES && same; at += sizeof block)
                same = fread(block, 1, sizeof block, old) == sizeof block &&
                    !memcmp(block, pixels + at, sizeof block);
            same = same && fgetc(old) == EOF && !ferror(old);
            fclose(old);
            if (same) return;
        }
    }
    if (file_state(stage) == REGULAR && unlink(stage)) return;
    FILE *output = open_new(stage);
    if (!output) return;
    bool ok = fwrite(pixels, 1, ICON_BYTES, output) == ICON_BYTES && flush(output);
    if (fclose(output)) ok = false;
    if (!ok || !control_publish(stage, to, directory)) unlink(stage);
#endif
}

void store_sync_icon(const Store *store, const App *app, const char *artwork) {
    if (app->state != INSTALLED || app->package.kind == PACKAGE_IZM) return;
    char from[STORE_PATH];
    if (!path(from, "%s/icons/%s.rgba", artwork, app->package.id) || file_state(from) != REGULAR) return;
    FILE *input = fopen(from, "rb");
    if (!input) return;
    unsigned char *pixels = malloc(ICON_BYTES);
    bool ok = pixels && fread(pixels, 1, ICON_BYTES, input) == ICON_BYTES &&
        fgetc(input) == EOF && !ferror(input);
    fclose(input);
    if (ok) store_sync_icon_pixels(store, app, pixels);
    free(pixels);
}

bool store_catalog_address(const char *input, char output[RELEASE_URL_MAX]) {
    while (*input && (unsigned char)*input <= 32) ++input;
    size_t size = strlen(input);
    while (size && (unsigned char)input[size - 1] <= 32) --size;
    if (size >= RELEASE_URL_MAX) return false;
    char value[RELEASE_URL_MAX];
    memcpy(value, input, size); value[size] = 0;
    if (!size) { output[0] = 0; return true; }
    const char prefix[] = "https://github.com/";
    if (!strncmp(value, prefix, sizeof prefix - 1)) {
        char *owner = value + sizeof prefix - 1, *repo = strchr(owner, '/');
        if (!repo || repo == owner || !repo[1]) return false;
        char *rest = strchr(repo + 1, '/');
        size_t repository = rest ? (size_t)(rest - owner) : strlen(owner);
        const char *file = "main/catalog/catalog.tsv";
        if (rest && !strncmp(rest, "/blob/", 6)) file = rest + 6;
        else if (rest && rest[1]) return false;
        if (snprintf(output, RELEASE_URL_MAX, "https://raw.githubusercontent.com/%.*s/%s",
                     (int)repository, owner, file) >= RELEASE_URL_MAX) return false;
    } else memcpy(output, value, size + 1);
    return catalog_url(output);
}

static const char settings_signature[] = "IZCATALOGS3";

unsigned store_enabled_online_catalogs(const StoreSettings *settings) {
    unsigned enabled = 0;
    for (unsigned i = 0; i < settings->count; ++i) enabled += settings->sources[i].enabled;
    return enabled;
}

unsigned store_community_catalog(const StoreSettings *settings) {
    for (unsigned i = 0; i < settings->count; ++i)
        if (settings->sources[i].community) return i;
    return settings->count;
}

static bool append_catalog(StoreSettings *settings, const char *input, bool enabled, bool community) {
    char url[RELEASE_URL_MAX];
    if (settings->count >= STORE_MAX_CATALOGS || !store_catalog_address(input, url) || !url[0]) return false;
    for (unsigned i = 0; i < settings->count; ++i)
        if (!strcmp(settings->sources[i].url, url) || (community && settings->sources[i].community)) return false;
    CatalogSource *source = &settings->sources[settings->count++];
    strcpy(source->url, url);
    source->enabled = enabled;
    source->community = community;
    return true;
}

static bool read_catalog_settings(FILE *file, StoreSettings *settings, unsigned version) {
    char line[768];
    if (!fgets(line, sizeof line, file) || !trim_line(line)) return false;
    bool old_sd_mode = version < 3 && !strcmp(line, "sd");
    if (version < 3) {
        if (!old_sd_mode && strcmp(line, "online")) return false;
        settings->sd_enabled = old_sd_mode;
    } else {
        if (strcmp(line, "sd-on") && strcmp(line, "sd-off")) return false;
        settings->sd_enabled = !strcmp(line, "sd-on");
    }
    while (fgets(line, sizeof line, file)) {
        char *values[2];
        if (!trim_line(line) || !parts(line, values, 2) || !values[0][0] || values[0][1] ||
            values[0][0] < '0' || values[0][0] > (version == 1 ? '1' : '3')) return false;
        unsigned flags = (unsigned)(values[0][0] - '0');
        bool community = (flags & 2u) || (version == 1 && !strcmp(values[1], STORE_COMMUNITY_CATALOG_URL));
        if (!append_catalog(settings, values[1], !old_sd_mode && (flags & 1u), community)) return false;
    }
    return feof(file) && !ferror(file);
}

static bool read_settings_file(const char *name, StoreSettings *settings, bool mode) {
    FILE *file = fopen(name, "rb");
    if (!file) return false;
    char line[768]; StoreSettings next = {0};
    bool ok = fgets(line, sizeof line, file) && (trim_line(line) || feof(file));
    if (ok && mode && !strcmp(line, settings_signature)) ok = read_catalog_settings(file, &next, 3);
    else if (ok && mode && !strcmp(line, "IZCATALOGS2")) ok = read_catalog_settings(file, &next, 2);
    else if (ok && mode && !strcmp(line, "IZCATALOGS1")) ok = read_catalog_settings(file, &next, 1);
    else if (ok) {
        char url[RELEASE_URL_MAX];
        ok = store_catalog_address(line, url);
        if (ok && url[0]) ok = append_catalog(&next, url, true, !strcmp(url, STORE_COMMUNITY_CATALOG_URL));
        next.sd_enabled = next.count == 0;
        if (ok && mode && fgets(line, sizeof line, file)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strcmp(line, "sd")) {
                next.sd_enabled = true;
                next.sources[0].enabled = false;
            } else if (!strcmp(line, "online")) next.sd_enabled = false;
        }
    }
    if (ok) *settings = next;
    fclose(file); return ok;
}

void store_settings_read(const char *root, StoreSettings *settings) {
    *settings = (StoreSettings){.sd_enabled = local_catalog_available(root)};
    char name[STORE_PATH];
    const char *files[] = {"settings.txt", "settings.txt.bak", "remote.tsv", "remote.tsv.bak"};
    for (unsigned i = 0; i < sizeof files / sizeof *files; ++i)
        if (path(name, "%s" STORE_DIR "/%s", root, files[i]) &&
            read_settings_file(name, settings, i < 2)) break;
    append_catalog(settings, STORE_COMMUNITY_CATALOG_URL, false, true);
}

static bool settings_folder(const char *root, const char *directory) {
    char parent[STORE_PATH];
    if (!path(parent, "%s/iz2s", root) || !folder(parent)) return false;
    return folder(directory);
}

bool store_settings_write(const char *root, const StoreSettings *settings, char out[STORE_MESSAGE]) {
    StoreSettings clean = {.sd_enabled = settings->sd_enabled};
    if (settings->count > STORE_MAX_CATALOGS) return message(out, "Too many catalog sources.");
    for (unsigned i = 0; i < settings->count; ++i)
        if (!append_catalog(&clean, settings->sources[i].url, settings->sources[i].enabled,
                            settings->sources[i].community))
            return message(out, "Use a valid, unique catalog URL.");
    char directory[STORE_PATH], stage[STORE_PATH], name[STORE_PATH];
    if (!path(directory, "%s" STORE_DIR, root) || !path(stage, "%s/.settings.tmp", directory) ||
        !path(name, "%s/settings.txt", directory) || !settings_folder(root, directory))
        return message(out, "Could not open the store settings folder on SD.");
    if (file_state(stage) == REGULAR && unlink(stage)) return message(out, "Could not clear temporary settings.");
    FILE *file = open_new(stage);
    if (!file) return message(out, "Could not open temporary settings on SD.");
    bool ok = fprintf(file, "%s\n%s\n", settings_signature, clean.sd_enabled ? "sd-on" : "sd-off") > 0;
    for (unsigned i = 0; ok && i < clean.count; ++i) {
        const CatalogSource *source = &clean.sources[i];
        unsigned flags = (unsigned)source->enabled + 2u * source->community;
        ok = fprintf(file, "%u\t%s\n", flags, source->url) > 0;
    }
    if (ok) ok = flush(file);
    if (fclose(file)) ok = false;
    if (ok && control_publish(stage, name, directory)) return true;
    unlink(stage);
    return message(out, "Could not save settings on SD.");
}

static bool read_remote(FILE *file, const char *url, Store *store) {
    char line[768];
    if (!fgets(line, sizeof line, file) || !trim_line(line) || strcmp(line, url)) return false;
    App *remote = NULL;
    unsigned count = 0;
    if (!parse_list(file, &remote, &count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        if (!remote[i].package.release_url[0]) { free(remote); return false; }
    }
    free(store->apps); store->apps = remote;
    store->count = count;
    return true;
}

static bool catalog_cache_path(char name[STORE_PATH], const char *root, const char *url, bool artwork) {
    Hash hash;
    if (!hash_open(&hash)) return false;
    uint8_t bytes[32];
    bool ok = hash_write(&hash, url, strlen(url));
    if (!hash_close(&hash, bytes) || !ok) return false;
    char digest[65];
    for (unsigned i = 0; i < sizeof bytes; ++i) snprintf(digest + 2*i, 3, "%02x", bytes[i]);
    return path(name, "%s" STORE_DIR "/%s-%s.%s", root,
                artwork ? "artwork" : "catalog", digest, artwork ? "bin" : "tsv");
}

static bool cached_list(Store *feed, const char *root, const char *url) {
    char names[2][STORE_PATH];
    if (!catalog_cache_path(names[0], root, url, false) ||
        !path(names[1], "%s" STORE_DIR "/remote.tsv", root)) return false;
    for (unsigned i = 0; i < 2; ++i) {
        FILE *file = control_read(names[i]);
        if (!file) continue;
        bool ok = read_remote(file, url, feed);
        fclose(file);
        if (ok) return true;
    }
    return false;
}

static bool catalog_sink(void *context, const void *bytes, size_t count) {
    return fwrite(bytes, 1, count, context) == count;
}

/* IZART1 keeps its byte count; IZART2 uses a little-endian 32-bit count. */
static unsigned read_artwork(FILE *file, const Store *store, const char *url, unsigned catalog,
                             unsigned wanted, uint32_t *pixels) {
    char line[768], magic[7];
    if (url && (!fgets(line, sizeof line, file) || !trim_line(line) || strcmp(line, url))) return 0;
    if (fread(magic, 1, sizeof magic, file) != sizeof magic) return 0;
    uint32_t count;
    if (!memcmp(magic, "IZART1\n", 7)) {
        int value = fgetc(file); if (value < 1) return 0; count = (unsigned)value;
    } else if (!memcmp(magic, "IZART2\n", 7)) {
        unsigned char bytes[4];
        if (fread(bytes, 1, 4, file) != 4) return 0;
        count = read_word(bytes); if (!count) return 0;
    } else return 0;
    struct stat info; long start = ftell(file);
    uint64_t bytes = (uint64_t)count * (ICON_ID_BYTES + ICON_BYTES);
    if (start < 0 || fstat(fileno(file), &info) || info.st_size < start ||
        (uint64_t)(info.st_size - start) != bytes) return 0;
    unsigned char *seen = store->count ? calloc(store->count, 1) : NULL;
    if (store->count && !seen) return 0;
    unsigned found = 0; bool ok = true;
    for (uint32_t item = 0; ok && item < count; ++item) {
        char id[ICON_ID_BYTES];
        if (fread(id, 1, sizeof id, file) != sizeof id || !memchr(id, 0, sizeof id) || !identifier(id)) { ok = false; break; }
        unsigned slot = 0;
        while (slot < store->count && (strcmp(store->apps[slot].package.id, id) ||
               (catalog != UINT_MAX && store->apps[slot].catalog != catalog))) ++slot;
        if (slot < store->count) {
            if (seen[slot]) { ok = false; break; }
            seen[slot] = 1;
            if (wanted == UINT_MAX || wanted == slot) ++found;
        }
        if (pixels && wanted == slot && slot < store->count) {
            if (fread(pixels, 1, ICON_BYTES, file) != ICON_BYTES) ok = false;
        } else if (fseek(file, ICON_BYTES, SEEK_CUR)) ok = false;
    }
    free(seen); return ok ? found : 0;
}

static unsigned read_artwork_path(const char *name, const Store *store, const char *url,
                                  unsigned catalog, unsigned wanted, uint32_t *pixels) {
    FILE *file = control_read(name);
    if (!file) return 0;
    unsigned count = read_artwork(file, store, url, catalog, wanted, pixels);
    fclose(file);
    return count;
}

static unsigned cached_artwork(const Store *store, const char *root, const char *url,
                               unsigned catalog, unsigned wanted, uint32_t *pixels) {
    char name[STORE_PATH];
    if (!catalog_cache_path(name, root, url, true)) return 0;
    unsigned count = read_artwork_path(name, store, url, catalog, wanted, pixels);
    if (count || !path(name, "%s" STORE_DIR "/remote-artwork.bin", root)) return count;
    return read_artwork_path(name, store, url, catalog, wanted, pixels);
}

static unsigned local_artwork(const Store *store, unsigned wanted, uint32_t *pixels) {
    char name[STORE_PATH];
    if (!path(name, "%s%s/artwork.bin", store->root, local_catalog_dir(store->root))) return 0;
    return read_artwork_path(name, store, NULL, LOCAL_CATALOG, wanted, pixels);
}

bool store_read_artwork_icon(const Store *store, unsigned slot, uint32_t *pixels) {
    if (slot >= store->count) return false;
    unsigned origin = store->apps[slot].catalog;
    if (origin == LOCAL_CATALOG) return local_artwork(store, slot, pixels) != 0;
    return origin < store->catalog_count &&
        cached_artwork(store, store->root, store->catalog_urls[origin], origin, slot, pixels) != 0;
}

unsigned store_read_artwork(const Store *store, uint32_t pixels[][STORE_ICON_SIZE * STORE_ICON_SIZE]) {
    unsigned count = 0;
    if (pixels) {
        for (unsigned i = 0; i < store->count; ++i) count += store_read_artwork_icon(store, i, pixels[i]);
    } else {
        for (unsigned i = 0; i < store->catalog_count; ++i)
            count += cached_artwork(store, store->root, store->catalog_urls[i], i, UINT_MAX, NULL);
        for (unsigned i = 0; i < store->count; ++i)
            if (store->apps[i].catalog == LOCAL_CATALOG) {
                count += local_artwork(store, UINT_MAX, NULL);
                break;
            }
    }
    return count;
}

static unsigned fetch_artwork(const Store *feed, const char *root, const char *source) {
    char url[RELEASE_URL_MAX], directory[STORE_PATH], stage[STORE_PATH], name[STORE_PATH];
    const char *end = strchr(source, '?');
    if (!end) end = source + strlen(source);
    const char *slash = end;
    while (slash > source && slash[-1] != '/') --slash;
    if (!feed->count ||
        snprintf(url, sizeof url, "%.*sartwork.bin", (int)(slash-source), source) >= (int)sizeof url ||
        !path(directory, "%s" STORE_DIR, root) ||
        !catalog_cache_path(name, root, source, true) ||
        !path(stage, "%s.tmp", name)) return 0;
    if (file_state(stage) == REGULAR && unlink(stage)) return 0;
    FILE *file = open_new(stage);
    if (!file) return 0;
    char error[DOWNLOAD_MESSAGE];
    bool ok = fprintf(file, "%s\n", source) > 0 &&
        catalog_download(url, UINT64_MAX, catalog_sink, file, error) && flush(file);
    if (fclose(file)) ok = false;
    unsigned slots = 0;
    if (ok && (file = fopen(stage, "rb")) != NULL) {
        slots = read_artwork(file, feed, source, UINT_MAX, UINT_MAX, NULL);
        fclose(file);
    }
    if (!slots || !control_publish(stage, name, directory)) {
        unlink(stage);
        return 0;
    }
    return slots;
}

static bool read_catalog_response(FILE *file, Store *feed, const char *url,
                                  char redirect[RELEASE_URL_MAX], char out[STORE_MESSAGE]) {
    char line[768];
    if (!fgets(line, sizeof line, file) || !trim_line(line) || strcmp(line, url) ||
        !fgets(line, sizeof line, file)) return message(out, "Invalid catalog.");
    const char prefix[] = "IZREDIRECT1\t";
    if (!strncmp(line, prefix, sizeof prefix - 1)) {
        if (!trim_line(line) || fgetc(file) != EOF || ferror(file) ||
            !store_catalog_address(line + sizeof prefix - 1, redirect) ||
            !catalog_same_origin(url, redirect))
            return message(out, "Invalid catalog redirect. Use one URL on the same origin.");
        return true;
    }
    if (fseek(file, 0, SEEK_SET) || !read_remote(file, url, feed))
        return message(out, "Invalid list or not enough memory.");
    return true;
}

static bool fetch_catalog_file(Store *feed, const char *root, const char *url,
                               char redirect[RELEASE_URL_MAX], char out[STORE_MESSAGE]) {
    char directory[STORE_PATH], stage[STORE_PATH], name[STORE_PATH];
    if (!path(directory, "%s" STORE_DIR, root) || !catalog_cache_path(name, root, url, false) ||
        !path(stage, "%s.tmp", name)) return message(out, "The SD path is too long.");
    if (!settings_folder(root, directory)) return message(out, "Could not open the catalog folder on SD.");
    if (file_state(stage) == REGULAR && unlink(stage)) return message(out, "Could not clear the temporary catalog.");
    FILE *file = open_new(stage);
    if (!file) return message(out, "Could not open a temporary catalog on SD.");
    bool ok = fprintf(file, "%s\n", url) > 0;
    if (ok) ok = catalog_download(url, UINT64_MAX, catalog_sink, file, out);
    else message(out, "Could not write the catalog on SD.");
    if (ok && !flush(file)) ok = message(out, "Could not save the catalog on SD.");
    if (fclose(file) && ok) ok = message(out, "Could not close the temporary catalog on SD.");
    if (ok) {
        file = fopen(stage, "rb");
        if (file) {
            ok = read_catalog_response(file, feed, url, redirect, out);
            fclose(file);
        } else ok = message(out, "Could not read the downloaded catalog.");
    }
    if (ok && !redirect[0] && !control_publish(stage, name, directory))
        ok = message(out, "Could not publish the catalog on SD.");
    if (!ok || redirect[0]) unlink(stage);
    if (!ok) store_clear(feed);
    return ok;
}

static bool fetch_catalog(Store *feed, const char *root, const char *url,
                          char resolved[RELEASE_URL_MAX], char out[STORE_MESSAGE]) {
    enum { MAX_REDIRECTS = 4 };
    char visited[MAX_REDIRECTS + 1][RELEASE_URL_MAX];
    strcpy(visited[0], url);
    for (unsigned i = 0; i <= MAX_REDIRECTS; ++i) {
        char next[RELEASE_URL_MAX] = {0};
        if (!fetch_catalog_file(feed, root, visited[i], next, out)) return false;
        if (!next[0]) {
            strcpy(resolved, visited[i]);
            return true;
        }
        for (unsigned j = 0; j <= i; ++j)
            if (!strcmp(visited[j], next)) return message(out, "Catalog redirect loop.");
        if (i == MAX_REDIRECTS) break;
        strcpy(visited[i + 1], next);
    }
    return message(out, "Too many catalog redirects.");
}

static bool merge_catalog(Store *store, Store *feed, const char *url) {
    unsigned origin = url ? store->catalog_count : LOCAL_CATALOG;
    if (!store->count) {
        store->apps = feed->apps;
        store->count = feed->count;
        feed->apps = NULL;
        for (unsigned i = 0; i < store->count; ++i) store->apps[i].catalog = origin;
        if (url) strcpy(store->catalog_urls[store->catalog_count++], url);
        return true;
    }
    if (feed->count > UINT_MAX - store->count ||
        (size_t)store->count + feed->count > SIZE_MAX / sizeof *store->apps) return false;
    App *apps = malloc(((size_t)store->count + feed->count) * sizeof *apps);
    if (!apps) return false;
    if (store->count) memcpy(apps, store->apps, (size_t)store->count * sizeof *apps);
    free(store->apps);
    store->apps = apps;
    if (url) strcpy(store->catalog_urls[store->catalog_count++], url);
    for (unsigned i = 0; i < feed->count; ++i) {
        const App *candidate = &feed->apps[i];
        unsigned slot = 0;
        while (slot < store->count && strcmp(apps[slot].package.id, candidate->package.id)) ++slot;
        if (slot == store->count) ++store->count;
        else if (preferred_kind(apps[slot].package.kind) || !preferred_kind(candidate->package.kind)) continue;
        apps[slot] = *candidate;
        apps[slot].catalog = origin;
        apps[slot].icon = (int)slot;
    }
    return true;
}

static void deduplicate_sources(StoreSettings *settings) {
    unsigned unique = 0;
    for (unsigned i = 0; i < settings->count; ++i) {
        CatalogSource *source = &settings->sources[i];
        unsigned slot = 0;
        while (slot < unique && strcmp(settings->sources[slot].url, source->url)) ++slot;
        if (slot == unique) {
            settings->sources[unique++] = *source;
        } else {
            settings->sources[slot].enabled |= source->enabled;
            settings->sources[slot].community |= source->community;
        }
    }
    settings->count = unique;
}

static bool collect_catalogs(Store *store, StoreSettings *settings, bool download,
                             CatalogRefreshMode mode, char out[STORE_MESSAGE]) {
    free(store->apps); store->apps = NULL;
    store->count = store->catalog_count = 0;
    store->local_catalog_available = local_catalog_available(store->root);
    unsigned online = store_enabled_online_catalogs(settings), enabled = online;
    unsigned loaded = 0, fetched = 0, failed = 0, thumbnails = 0;
    bool moved = false;
    StoreSettings updated = *settings;
    char error[STORE_MESSAGE] = {0};
    for (unsigned i = 0; i < settings->count; ++i) {
        const CatalogSource *source = &settings->sources[i];
        if (!source->enabled) continue;
        const char *url = source->url;
        Store feed = {0};
        bool available = mode == CATALOG_CACHED && cached_list(&feed, store->root, url);
        unsigned images = 0;
        if (download && !available) {
            char failure[STORE_MESSAGE] = {0};
            char *resolved = updated.sources[i].url;
            available = fetch_catalog(&feed, store->root, url, resolved, failure);
            if (available) {
                ++fetched;
                moved |= strcmp(url, resolved) != 0;
                url = resolved;
                if (mode != CATALOG_REFRESH)
                    images = cached_artwork(&feed, store->root, url, UINT_MAX, UINT_MAX, NULL);
                if (!images) images = fetch_artwork(&feed, store->root, url);
            } else {
                ++failed;
                if (!error[0]) strcpy(error, failure);
                available = cached_list(&feed, store->root, url);
            }
        } else if (download) {
            images = cached_artwork(&feed, store->root, url, UINT_MAX, UINT_MAX, NULL);
            if (!images) images = fetch_artwork(&feed, store->root, url);
        }
        bool merged = !available || merge_catalog(store, &feed, url);
        store_clear(&feed);
        if (!merged) return message(out, "Not enough memory to combine catalogs.");
        loaded += available;
        thumbnails += images;
    }
    if (settings->sd_enabled) {
        Store feed = {0};
        char path_to_catalog[STORE_PATH], failure[STORE_MESSAGE] = {0};
        bool ok = path(path_to_catalog, "%s%s/catalog.tsv", store->root, local_catalog_dir(store->root)) &&
            load_local_list(&feed, path_to_catalog, failure);
        if (ok && feed.count) {
            ok = merge_catalog(store, &feed, NULL);
            if (ok) { ++loaded; ++enabled; }
            else snprintf(failure, sizeof failure, "Not enough memory to combine catalogs.");
        }
        store_clear(&feed);
        if (!ok) {
            ++enabled; ++failed;
            if (!error[0]) snprintf(error, sizeof error, "%s", failure[0] ? failure : "Could not read the SD catalog.");
        }
    }
    out[0] = '\0';
    if (!download) return loaded || !failed || message(out, error);
    if (moved) {
        deduplicate_sources(&updated);
        if (!store_settings_write(store->root, &updated, out)) return false;
        *settings = updated;
    }
    if (!online) {
        if (failed) return message(out, error);
        if (settings->sd_enabled) {
            if (!loaded) snprintf(out, STORE_MESSAGE, "Copy catalog.tsv to the Cheese catalog folder on SD.");
            else if (mode == CATALOG_REFRESH) snprintf(out, STORE_MESSAGE, "SD catalog reloaded. %u apps.", store->count);
        }
        return true;
    }
    if (mode == CATALOG_CACHED && !fetched && !failed) return true;
    if (failed && !loaded) return message(out, error[0] ? error : "Could not download catalogs.");
    if (enabled > 1)
        snprintf(out, STORE_MESSAGE, "%u apps from %u/%u catalogs.%s%s", store->count, loaded, enabled,
                 moved ? " Catalog address updated." : "",
                 failed ? " Some sources could not be refreshed. Available caches were kept." : "");
    else if (failed)
        snprintf(out, STORE_MESSAGE, "Update failed; showing cached catalog. %.119s", error);
    else if (moved)
        snprintf(out, STORE_MESSAGE, "Catalog address updated. %u apps loaded.", store->count);
    else if (thumbnails)
        snprintf(out, STORE_MESSAGE, "Catalog updated: %u online entries, %u thumbnail%s.",
                 store->count, thumbnails, thumbnails == 1 ? "" : "s");
    else
        snprintf(out, STORE_MESSAGE, "Catalog updated: %u online entries. No new thumbnails received.", store->count);
    return !failed || (mode == CATALOG_CACHED && loaded);
}

bool store_fetch_list(Store *store, StoreSettings *settings, char out[STORE_MESSAGE]) {
    if (!settings->sd_enabled && !store_enabled_online_catalogs(settings))
        return message(out, "Open Catalogs to enable a source.");
    return collect_catalogs(store, settings, true, CATALOG_REFRESH, out);
}

bool store_refresh_catalog(Store *store, const char *root, StoreSettings *settings,
                           CatalogRefreshMode mode, char out[STORE_MESSAGE]) {
    return load_catalog(store, root, settings, true, mode, out);
}

static bool stage_file(const char *source, const char *stage, const PackageFile *file,
                       Progress progress, uint64_t done, uint64_t total) {
    FILE *output = open_new(stage);
    if (!output) return false;
    char digest[65];
    bool ok = transfer(source, output, file->bytes, digest, progress, done, total) &&
              !strcmp(file->digest, digest) && flush(output);
    if (fclose(output) != 0) ok = false;
    if (ok) ok = matches(stage, file);
    if (!ok) unlink(stage);
    return ok;
}

typedef struct {
    FILE *output;
    Progress progress;
    uint64_t done, total;
    bool write_failed;
} DownloadStage;

static bool stage_write(void *context, const void *bytes, size_t count) {
    DownloadStage *transfer = context;
    if (cancelled(transfer->progress)) return false;
    if (fwrite(bytes, 1, count, transfer->output) != count) {
        transfer->write_failed = true;
        return false;
    }
    transfer->done += count;
    if (transfer->progress.report)
        transfer->progress.report(transfer->progress.context, transfer->done, transfer->total);
    return true;
}

static bool stage_github(const Package *package, unsigned index, const char *stage, Progress progress,
                         uint64_t done, uint64_t total, char out[STORE_MESSAGE]) {
    const PackageFile *file = &package->files[index];
    char asset_url[RELEASE_URL_MAX];
    const char *url = package->release_url;
    if (index) {
        const char *name = strrchr(url, '/') + 1;
        int length = snprintf(asset_url, sizeof asset_url, "%.*s%s", (int)(name - url), url, file->name);
        if (length < 0 || (size_t)length >= sizeof asset_url)
            return message(out, "The GitHub resource URL is too long.");
        url = asset_url;
    }
    FILE *output = open_new(stage);
    if (!output) return message(out, "Could not open a temporary file on SD.");
    DownloadStage transfer = {.output = output, .progress = progress, .done = done, .total = total};
    bool ok = github_download(url, file->bytes, stage_write, &transfer, progress.cancel, out);
    if (transfer.write_failed || (ok && !flush(output))) ok = message(out, "Could not write the download to SD.");
    if (fclose(output) && ok) ok = message(out, "Could not finish writing the download to SD.");
    if (ok && !matches(stage, file)) ok = message(out, "The download failed its SHA-256 check.");
    if (cancelled(progress)) ok = message(out, "Download cancelled.");
    if (!ok) unlink(stage);
    return ok;
}

static bool write_receipt(const char *directory, const Package *package, const char *executable) {
    char final[STORE_PATH], stage[STORE_PATH];
    path(final, "%s/.izstore-%s.tsv", directory, package->files[0].digest);
    if (file_state(final) != ABSENT) {
        App recorded;
        if (!read_receipt(directory, package, executable, &recorded) || recorded.package.count != package->count)
            return false;
        for (unsigned i = 0; i < package->count; ++i) {
            const PackageFile *old = &recorded.package.files[i], *file = &package->files[i];
            if (strcmp(old->name, file->name) || strcmp(old->digest, file->digest) || old->bytes != file->bytes) return false;
        }
        return true;
    }
    path(stage, "%s/.izstore-receipt.tmp", directory);
    if (file_state(stage) == REGULAR && unlink(stage)) return false;
    FILE *file = open_new(stage);
    if (!file) return false;
    bool ok = fprintf(file, "%s\napp\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", signature, package->id,
        kind_field(package->kind), package->version, package->title,
        package->author, package->category, package->description) > 0;
    for (unsigned i = 0; ok && i < package->count; ++i) {
        const PackageFile *entry = &package->files[i];
        ok = fprintf(file, "file\t%llu\t%s\t%s\t%u\n", (unsigned long long)entry->bytes,
            entry->digest, entry->name, (package->owned >> i) & 1u) > 0;
    }
    ok = ok && flush(file);
    if (fclose(file) != 0) ok = false;
    if (ok) ok = rename(stage, final) == 0 && commit(directory);
    if (!ok) unlink(stage);
    return ok;
}

static bool owns(const Package *package, const char *name) {
    for (unsigned i = 1; i < package->count; ++i)
        if ((package->owned & (UINT32_C(1) << i)) && !strcmp(package->files[i].name, name)) return true;
    return false;
}

static bool uses(const Package *package, const char *name) {
    if (package)
        for (unsigned i = 1; i < package->count; ++i)
            if (!strcmp(package->files[i].name, name)) return true;
    return false;
}

static void cleanup_old(const char *directory, const Package *previous, const Package *next) {
    char location[STORE_PATH];
    for (unsigned i = 1; i < previous->count; ++i) {
        if (!(previous->owned & (UINT32_C(1) << i)) || uses(next, previous->files[i].name)) continue;
        path(location, "%s/%s", directory, previous->files[i].name);
        if (matches(location, &previous->files[i])) unlink(location);
    }
    if (!next || strcmp(previous->files[0].digest, next->files[0].digest)) {
        path(location, "%s/.izstore-%s.tsv", directory, previous->files[0].digest);
        if (file_state(location) == REGULAR) unlink(location);
    }
}

#ifdef IZ_STORE_STANDALONE
static bool update_store_nro(const Store *store, const Package *package, Progress progress,
                             char out[STORE_MESSAGE]) {
    if (!self_update_available(store, package)) return message(out, "Open the official release to update IZ2S Cheese.");
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH], stage[STORE_PATH];
    module_paths(store, package, directory, active, backup);
    if (!path(stage, "%s/.store-update.tmp", directory) ||
        !path(backup, "%s/.store-previous.nro.bak", directory))
        return message(out, "IZ2S Cheese path is too long.");
    if (file_state(active) != REGULAR || file_state(backup) == OTHER || file_state(stage) == OTHER)
        return message(out, "Could not update IZ2S Cheese at its standard SD path.");
    if (matches(active, &package->files[0]))
        return message(out, "This update is already saved. Reopen IZ2S Cheese.");
    if (file_state(stage) == REGULAR && unlink(stage))
        return message(out, "Could not clear an interrupted update.");
    if (!stage_github(package, 0, stage, progress, 0, package->files[0].bytes, out)) return false;
    if (cancelled(progress) || !valid_image(stage, package)) {
        unlink(stage);
        return message(out, cancelled(progress) ? "Download cancelled." : "The Cheese update is incompatible.");
    }
    if ((file_state(backup) == REGULAR && unlink(backup)) || rename(active, backup)) {
        unlink(stage);
        return message(out, "Could not keep a copy of the previous Store.");
    }
    if (rename(stage, active)) {
        rename(backup, active);
        unlink(stage);
        commit(directory);
        return message(out, "Could not install the Cheese update.");
    }
    if (!commit(directory)) return message(out, "Could not confirm the update on SD.");
    snprintf(out, STORE_MESSAGE, "Updated to %s. Reopen IZ2S Cheese.", package->version);
    return true;
}
#endif

bool store_install(const Store *store, const App *app, Progress progress, char out[STORE_MESSAGE]) {
    Package package = app->package;
    if (is_store_entry(&package)) {
#ifdef IZ_STORE_STANDALONE
        return update_store_nro(store, &package, progress, out);
#else
        if (!self_update_available(store, &package))
            return message(out, "No compatible official Cheese update in this catalog.");
#endif
    }
#ifdef IZ_STORE_STANDALONE
    if (package.kind == PACKAGE_IZM) return message(out, "IZA apps require IZ2S.");
#endif
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH], stage[STORE_PATH], base[STORE_PATH];
    module_paths(store, &package, directory, active, backup);
    path(base, "%s" INSTALL_DIR, store->root);
#ifdef IZ_STORE_STANDALONE
    if (file_state(active) == ABSENT && file_state(backup) == ABSENT && flat_install(store, &package) != ABSENT)
        return message(out, "A separate copy already exists in /switch.");
#endif
    if (!folder(base) || !folder(directory)) return message(out, "Could not create the application folder.");
#ifndef IZ_STORE_STANDALONE
    if (conflict(directory, &package)) return message(out, "The application folder has conflicting files.");
#endif
    bool self = is_store_entry(&package);
    if (self ? file_state(backup) != ABSENT : !recover(directory, active, backup, &package))
        return message(out, "The application folder has conflicting files.");
    bool exists = file_state(active) != ABSENT;
    App old = {0};
    if (exists && !read_receipt(directory, &package, active, &old) && !self)
        return message(out, "This copy is managed outside this store.");

    uint64_t total = 0;
    for (unsigned i = 0; i < package.count; ++i) total += package.files[i].bytes;
    path(stage, "%s/.izstore-launcher.tmp", directory);
    if (file_state(stage) == REGULAR && unlink(stage))
        return message(out, "Could not clear an interrupted transfer.");
    char payload[STORE_PATH], source[STORE_PATH], target[STORE_PATH], copy[STORE_PATH];
    path(payload, "%s" STORE_DIR "/payloads/%s/%s", store->root, package.id, package.files[0].digest);
    path(source, "%s/%s", payload, package.files[0].name);
    if (package.release_url[0]) {
        if (!stage_github(&package, 0, stage, progress, 0, total, out)) return false;
    } else if (!stage_file(source, stage, &package.files[0], progress, 0, total))
        return message(out, "The local package is missing, damaged, or SD is full.");
    if (cancelled(progress)) { unlink(stage); return message(out, "Download cancelled."); }
    if (!valid_image(stage, &package)) {
        unlink(stage);
        return message(out, "The executable is incompatible with this catalog entry.");
    }
    package.owned = 1;
    path(copy, "%s/.izstore-resource.tmp", directory);
    if (file_state(copy) == REGULAR && unlink(copy)) {
        unlink(stage);
        return message(out, "Could not clear an interrupted resource transfer.");
    }
    uint64_t done = package.files[0].bytes;
    uint32_t added = 0;
    const char *failure = NULL;
    for (unsigned i = 1; i < package.count; ++i) {
        const PackageFile *file = &package.files[i];
        path(target, "%s/%s", directory, file->name);
        if (file_state(target) != ABSENT) {
            if (!matches(target, file)) { failure = "An installed resource failed its checksum."; break; }
            if (owns(&old.package, file->name)) package.owned |= UINT32_C(1) << i;
        } else {
            if (package.release_url[0]) {
                if (!stage_github(&package, i, copy, progress, done, total, out)) {
                    failure = out;
                    break;
                }
            } else {
                path(source, "%s/%s", payload, file->name);
                if (!stage_file(source, copy, file, progress, done, total)) {
                    failure = "A local resource is missing, damaged, or SD is full.";
                    break;
                }
            }
            if (rename(copy, target)) { failure = "Could not publish the verified resource."; break; }
            added |= UINT32_C(1) << i;
            package.owned |= UINT32_C(1) << i;
            if (!commit(directory)) { failure = "Could not confirm the resource on SD."; break; }
        }
        done += file->bytes;
    }
    if (cancelled(progress)) failure = "Download cancelled.";
    if (!failure && !write_receipt(directory, &package, stage)) failure = "Could not record the verified package.";
    if (failure) {
        for (unsigned i = 1; i < package.count; ++i) {
            if (!(added & (UINT32_C(1) << i))) continue;
            path(target, "%s/%s", directory, package.files[i].name);
            if (matches(target, &package.files[i])) unlink(target);
        }
        if (file_state(copy) == REGULAR) unlink(copy);
        unlink(stage);
        commit(directory);
        return failure == out ? false : message(out, failure);
    }
    if (exists && (rename(active, backup) || !commit(directory)))
        return message(out, "Could not keep the previous version.");
    if (rename(stage, active)) {
        if (exists) {
            if (self) { rename(backup, active); commit(directory); }
            else recover(directory, active, backup, &package);
        }
        return message(out, "Could not activate the package.");
    }
    if (!commit(directory)) return message(out, "The SD card did not confirm the installation.");
    if (self && exists) {
        char previous[STORE_PATH];
        path(previous, "%s/.store-previous.iza.bak", directory);
        if (file_state(previous) == REGULAR) unlink(previous);
        if (rename(backup, previous)) return message(out, "Update saved; previous copy remains in the recovery file.");
        /* Keep old content-addressed resources so the backup remains launchable. */
    } else if (exists && unlink(backup) == 0) cleanup_old(directory, &old.package, &package);
    if (!commit(directory)) return message(out, "The SD card did not confirm cleanup.");
#ifndef IZ_STORE_STANDALONE
    if (package.kind != PACKAGE_IZM) {
        uint32_t *pixels = malloc(ICON_BYTES);
        if (pixels && read_program_icon(active, false, pixels)) {
            App installed = {.package = package, .state = INSTALLED};
            store_sync_icon_pixels(store, &installed, pixels);
        }
        free(pixels);
    }
#endif
    snprintf(out, STORE_MESSAGE, "%s %s%s", exists ? "Updated to" : "Installed", package.version,
             self ? ". Reopen IZ2S Cheese." : "");
    return true;
}

bool store_remove(const Store *store, const App *app, char out[STORE_MESSAGE]) {
    if (is_store_entry(&app->package)) return message(out, "Exit IZ2S Cheese before removing it.");
#ifdef IZ_STORE_STANDALONE
    if (app->package.kind == PACKAGE_IZM) return message(out, "IZA apps require IZ2S.");
#endif
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH];
    module_paths(store, &app->package, directory, active, backup);
    App receipt;
    if (!recover(directory, active, backup, &app->package) ||
        !read_receipt(directory, &app->package, active, &receipt))
        return message(out, "This copy is managed outside this store.");
    if (unlink(active) || !commit(directory)) return message(out, "Could not remove the application.");
#ifndef IZ_STORE_STANDALONE
    if (app->package.kind != PACKAGE_IZM) {
        static const char *const icons[] = {".izcheese-icon.rgba", ".izcheese-icon.rgba.bak"};
        for (size_t i = 0; i < sizeof icons / sizeof *icons; ++i) {
            char icon[STORE_PATH];
            if (path(icon, "%s/%s", directory, icons[i]) && file_state(icon) == REGULAR)
                unlink(icon);
        }
    }
#endif
    cleanup_old(directory, &receipt.package, NULL);
    if (!commit(directory)) return message(out, "The SD card did not confirm cleanup.");
    snprintf(out, STORE_MESSAGE, "Removed. Saved data is still on SD.");
    return true;
}
