#include "ps2_sb4_layout.h"
#include "ps2_sb4_fs.h"
#include <sb4/native-app.h>
#include <stdio.h>
#include <stdlib.h>

extern void izps2_boot_stage(IzNativeApp *, const char *, uint32_t);
#include <string.h>
#ifdef IZPS2_WRAPPER_AUDIT
#include "native-system.h"
#endif

#define TRANSFER_BYTES (1024u * 1024u)
#define FS_PATH_NOT_FOUND 0x202u
#define FS_PATH_EXISTS 0x402u
static const char cards_root[] = PS2_SB4_RUNTIME_DIR "/memcards";
static const char states_root[] = PS2_SB4_RUNTIME_DIR "/sstates";
static const char gzip_cache_root[] = PS2_SB4_RUNTIME_DIR "/gzip-index";

static IzNativeApp *module;
static const Sb4StorageFiles *storage;
static uint32_t open_files, close_error;
#ifdef IZPS2_WRAPPER_AUDIT
static Ps2Sb4ReadTiming read_timing;

Ps2Sb4ReadTiming ps2_sb4_fs_read_timing(void) {
    return (Ps2Sb4ReadTiming){
        __atomic_load_n(&read_timing.calls, __ATOMIC_RELAXED),
        __atomic_load_n(&read_timing.bytes, __ATOMIC_RELAXED),
        __atomic_load_n(&read_timing.ticks, __ATOMIC_RELAXED),
        __atomic_load_n(&read_timing.errors, __ATOMIC_RELAXED),
    };
}
#endif

enum { SPLIT_PARTS = 64 };
static const uint64_t split_tag = UINT64_C(1) << 63;
typedef struct {
    uint32_t token;
    uint64_t size;
} SplitPart;
typedef struct {
    uint32_t count;
    uint64_t size;
    SplitPart parts[SPLIT_PARTS];
} SplitImage;

static size_t split_part_path(char path[SB4_STORAGE_NAME_MAX], const char *name) {
    if (strncmp(name, "games/", 6)) return 0;
    const size_t length = strlen(name);
    if (length + sizeof ".part000" > SB4_STORAGE_NAME_MAX) return 0;
    memcpy(path, name, length);
    memcpy(path + length, ".part000", sizeof ".part000");
    return length;
}

static void split_part_number(char *suffix, uint32_t index) {
    suffix[5] = '0' + index / 100;
    suffix[6] = '0' + index / 10 % 10;
    suffix[7] = '0' + index % 10;
}

static uint32_t probe_split(const Sb4Storage *metadata, const char *name, SplitImage *image) {
    char path[SB4_STORAGE_NAME_MAX];
    const size_t prefix = split_part_path(path, name);
    if (!metadata || !prefix) return FS_PATH_NOT_FOUND;

    for (uint32_t i = 0; i <= SPLIT_PARTS; ++i) {
        split_part_number(path + prefix, i);
        uint64_t size = 0;
        uint32_t rc = metadata->file_size(module, path, &size);
        if (rc == FS_PATH_NOT_FOUND) {
            if (!i) return rc;
            continue;
        }
        if (rc) return rc;
        if (i != image->count || i == SPLIT_PARTS || !size || size > INT64_MAX - image->size)
            return PS2_FS_BAD_REPLY;
        image->parts[image->count++].size = size;
        image->size += size;
    }
    return 0;
}

static SplitImage *split_image(uint64_t token) {
    // Split tokens are created and kept by the private Ps2SdkFs bridge.
    return token & split_tag ? (SplitImage *)(uintptr_t)(token & ~split_tag) : NULL;
}

static void close_storage_token(uint32_t token) {
    uint32_t result = storage->close(module, token);
    if (!result) {
        __atomic_sub_fetch(&open_files, 1, __ATOMIC_RELAXED);
        return;
    }
    uint32_t unset = 0;
    __atomic_compare_exchange_n(&close_error, &unset, result, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

static void close_split(SplitImage *image) {
    for (uint32_t i = 0; i < image->count; ++i)
        if (image->parts[i].token) close_storage_token(image->parts[i].token);
    free(image);
}

static uint32_t open_split(uint64_t *out, const char *name) {
    SplitImage *image = calloc(1, sizeof *image);
    if (!image) return SB4_STORAGE_LIMIT;
    uint32_t rc = probe_split(sb4ModuleStorage(module), name, image);
    if (!rc) {
        char path[SB4_STORAGE_NAME_MAX];
        const size_t prefix = split_part_path(path, name);
        for (uint32_t i = 0; !rc && i < image->count; ++i) {
            split_part_number(path + prefix, i);
            SplitPart *part = &image->parts[i];
            uint64_t actual = 0;
            rc = storage->open(module, path, SB4_STORAGE_OPEN_READ, &part->token, &actual);
            if (!rc) {
                if (part->token) __atomic_add_fetch(&open_files, 1, __ATOMIC_RELAXED);
                if (!part->token || actual != part->size) rc = PS2_FS_BAD_REPLY;
            } else part->token = 0;
        }
    }
    if (rc) close_split(image);
    else *out = split_tag | (uintptr_t)image;
    return rc;
}

static int matches_root(const char *name, const char *root, size_t length) {
    return !strncmp(name, root, length) && (!name[length] || name[length] == '/');
}

static const char *relative_path(const char *path, int allow_directory) {
    static const char disc_root[] = PS2_SB4_SDK_ROOT "/";
    static const char saves_root[] = "ps2sd:" PS2_SB4_SD_ROOT "/";
    static const char card_root[] = PS2_SB4_SD_ROOT "/";
    static const char resources_alias[] = "resources";
    static const char runtime_root[] = PS2_SB4_RUNTIME_DIR;
    if (!path) return NULL;
    if (!strncmp(path, disc_root, sizeof disc_root - 1)) path += sizeof disc_root - 1;
    else if (!strncmp(path, saves_root, sizeof saves_root - 1)) path += sizeof saves_root - 1;
    else if (!strncmp(path, card_root, sizeof card_root - 1)) path += sizeof card_root - 1;
    else if (*path == '/' && matches_root(path + 1, resources_alias, sizeof resources_alias - 1)) ++path;
    else if (*path == '/' && (matches_root(path + 1, cards_root, sizeof cards_root - 1) ||
                             matches_root(path + 1, states_root, sizeof states_root - 1) ||
                             matches_root(path + 1, gzip_cache_root, sizeof gzip_cache_root - 1) ||
                             matches_root(path + 1, runtime_root, sizeof runtime_root - 1))) ++path;
    else if (*path == '/' || !strncmp(path, "sdmc:", 5)) {
        const Sb4StorageRelativePath resolve = sb4StoragePathResolver(sb4ModuleStorage(module));
        path = resolve ? resolve(module, path) : NULL;
        if (!path) return NULL;
    }
    if (allow_directory && (!strcmp(path, "bios") || !strcmp(path, "games") ||
                            !strcmp(path, resources_alias))) return path;
    if ((!strncmp(path, "games/", 6) && path[6]) ||
        (!strncmp(path, "bios/", 5) && path[5]) ||
        (!strncmp(path, "saves/", 6) && path[6]) ||
        (!strncmp(path, "resources/", sizeof resources_alias) && path[sizeof resources_alias])) return path;
    return NULL;
}

static int valid_token(uint64_t token) {
    return storage && token && token <= UINT32_MAX;
}

static void enter(uint64_t *previous) { *previous = 0; }
static void restore(uint64_t previous) { (void)previous; }

static uint32_t open_file(uint64_t *out, const char *path, int mode) {
    if (!out) return PS2_FS_INVALID;
    *out = 0;
    const char *name = relative_path(path, 0);
    if (!storage || !name || mode < 1 || mode > 3) return PS2_FS_INVALID;
    const int writing = (mode & PS2_FS_WRITE) != 0;
    if (writing && strncmp(name, "saves/", 6)) return PS2_FS_READ_ONLY;
    uint32_t token = 0;
    uint64_t size = 0;
    uint32_t result = storage->open(module, name,
        writing ? SB4_STORAGE_OPEN_UPDATE : SB4_STORAGE_OPEN_READ, &token, &size);
    if (result == FS_PATH_NOT_FOUND && !writing && !strncmp(name, "games/", 6))
        return open_split(out, name);
    if (result) return result;
    if (!token) return PS2_FS_BAD_REPLY;
    __atomic_add_fetch(&open_files, 1, __ATOMIC_RELAXED);
    *out = token;
    return 0;
}

static void close_file(uint64_t token) {
    SplitImage *image = split_image(token);
    if (image) { close_split(image); return; }
    if (valid_token(token)) { close_storage_token((uint32_t)token); return; }
    uint32_t unset = 0;
    uint32_t result = PS2_FS_INVALID;
    __atomic_compare_exchange_n(&close_error, &unset, result, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

static uint32_t get_size(int64_t *out, uint64_t token) {
    if (!out) return PS2_FS_INVALID;
    const SplitImage *image = split_image(token);
    if (image) { *out = (int64_t)image->size; return 0; }
    if (!valid_token(token)) return PS2_FS_INVALID;
    uint64_t size = 0;
    uint32_t result = storage->size(module, (uint32_t)token, &size);
    if (result) return result;
    if (size > INT64_MAX) return PS2_FS_BAD_REPLY;
    *out = (int64_t)size;
    return 0;
}

static uint32_t read_part(size_t *out, uint32_t token, uint64_t offset, void *data, size_t bytes) {
    *out = 0;
    unsigned char *destination = data;
    size_t transferred = 0;
    while (transferred < bytes) {
        size_t left = bytes - transferred;
        uint32_t request = left > TRANSFER_BYTES ? TRANSFER_BYTES : (uint32_t)left;
        uint32_t actual = 0;
#ifdef IZPS2_WRAPPER_AUDIT
        const uint64_t start = iz_system_ticks();
#endif
        uint32_t result = storage->read(module, token, offset + transferred,
            destination + transferred, request, &actual);
#ifdef IZPS2_WRAPPER_AUDIT
        __atomic_fetch_add(&read_timing.ticks, iz_system_ticks() - start, __ATOMIC_RELAXED);
        __atomic_fetch_add(&read_timing.calls, 1, __ATOMIC_RELAXED);
        if (result || actual > request)
            __atomic_fetch_add(&read_timing.errors, 1, __ATOMIC_RELAXED);
        else
            __atomic_fetch_add(&read_timing.bytes, actual, __ATOMIC_RELAXED);
#endif
        if (result) return result;
        if (actual > request) return PS2_FS_BAD_REPLY;
        transferred += actual;
        if (actual < request) break;
    }
    *out = transferred;
    return 0;
}

static uint32_t read_file(size_t *out, uint64_t token, int64_t offset, void *data, size_t bytes) {
    if (!out) return PS2_FS_INVALID;
    *out = 0;
    if ((!data && bytes) || offset < 0 || bytes > (uint64_t)(INT64_MAX - offset)) return PS2_FS_INVALID;
    const SplitImage *image = split_image(token);
    if (!image) return valid_token(token) ? read_part(out, (uint32_t)token, offset, data, bytes) : PS2_FS_INVALID;

    size_t done = 0;
    uint64_t start = (uint64_t)offset;
    for (uint32_t i = 0; i < image->count && done < bytes; ++i) {
        const SplitPart *part = &image->parts[i];
        if (start >= part->size) {
            start -= part->size;
            continue;
        }
        const uint64_t remaining = part->size - start;
        const size_t count = remaining < bytes - done ? (size_t)remaining : bytes - done;
        size_t actual;
        uint32_t rc = read_part(&actual, part->token, start, (uint8_t *)data + done, count);
        if (rc || actual != count) return rc ? rc : PS2_FS_BAD_REPLY;
        done += actual;
        start = 0;
    }
    *out = done;
    return 0;
}

static uint32_t flush_file(uint64_t token) {
    if (!valid_token(token)) return PS2_FS_INVALID;
    uint32_t result = storage->flush(module, (uint32_t)token);
    return result ? result : storage->commit(module);
}

static uint32_t write_file(uint64_t token, int64_t offset, const void *data, size_t bytes,
                           const uint32_t *option) {
    if (!valid_token(token) || (!data && bytes) || !option || *option > 1 ||
        offset < 0 || bytes > (uint64_t)(INT64_MAX - offset)) return PS2_FS_INVALID;
    const unsigned char *source = data;
    for (size_t transferred = 0; transferred < bytes;) {
        size_t left = bytes - transferred;
        uint32_t request = left > TRANSFER_BYTES ? TRANSFER_BYTES : (uint32_t)left;
        uint32_t result = storage->write(module, (uint32_t)token, (uint64_t)offset + transferred,
            source + transferred, request);
        if (result) return result;
        transferred += request;
    }
    return *option ? flush_file(token) : 0;
}

static uint32_t resize_file(uint64_t token, int64_t size) {
    return valid_token(token) && size >= 0 ? storage->resize(module, (uint32_t)token, (uint64_t)size)
                                         : PS2_FS_INVALID;
}

static uint32_t create_file(const char *path, int64_t size, int option) {
    const char *name = relative_path(path, 0);
    if (!storage || !name || size < 0 || option) return PS2_FS_INVALID;
    if (strncmp(name, "saves/", 6)) return PS2_FS_READ_ONLY;
    return storage->create(module, name, (uint64_t)size);
}

int ps2_sb4_fs_is_not_found(uint32_t result) { return result == FS_PATH_NOT_FOUND; }

const Ps2SdkFsApi *ps2_sb4_fs_bind(IzNativeApp *app) {
    static const Ps2SdkFsApi api = {
        .enter = enter, .restore = restore, .open = open_file, .close = close_file,
        .get_size = get_size, .read = read_file, .write = write_file, .flush = flush_file,
        .set_size = resize_file, .create = create_file, .is_not_found = ps2_sb4_fs_is_not_found,
    };
    const Sb4StorageFiles *files = sb4StorageFiles(sb4ModuleStorage(app));
    if (!files || module) return NULL;
#ifdef IZPS2_WRAPPER_AUDIT
    read_timing = (Ps2Sb4ReadTiming){0};
#endif
    storage = files;
    module = app;
    return &api;
}

uint32_t ps2_sb4_fs_unbind(void) {
    const uint32_t error = __atomic_load_n(&close_error, __ATOMIC_RELAXED);
    const uint32_t open = __atomic_load_n(&open_files, __ATOMIC_RELAXED);
    char outcome[112];
    const int size = snprintf(outcome, sizeof(outcome), "36 SD unbind open=%u close_error=0x%08x\n", open, error);
    if (size > 0 && (size_t)size < sizeof(outcome))
        izps2_boot_stage(NULL, outcome, (uint32_t)size);
    if (error) return error;
    if (!module || open) return PS2_FS_INVALID;
    module = NULL;
    storage = NULL;
    return 0;
}

uint32_t ps2_sb4_fs_path_size(const char *path, uint64_t *bytes) {
    if (!bytes) return PS2_FS_INVALID;
    *bytes = 0;
    const char *name = relative_path(path, 0);
    const Sb4Storage *metadata = sb4ModuleStorage(module);
    if (!metadata || !name) return PS2_FS_INVALID;
    uint32_t rc = metadata->file_size(module, name, bytes);
    if (rc != FS_PATH_NOT_FOUND) return rc;
    SplitImage image = {0};
    rc = probe_split(metadata, name, &image);
    if (!rc) *bytes = image.size;
    return rc;
}

uint32_t ps2_sb4_fs_stat(const char *path, Sb4StorageInfo *info) {
    if (!info) return PS2_FS_INVALID;
    *info = (Sb4StorageInfo){0};
    const Sb4Storage *metadata = sb4ModuleStorage(module);
    const char *name = relative_path(path, 1);
    if (!metadata || !name) return PS2_FS_INVALID;
    Sb4StorageStatFunction stat_path = sb4StorageStat(metadata);
    if (stat_path) {
        uint32_t rc = stat_path(module, name, info);
        return rc == FS_PATH_NOT_FOUND ? ps2_sb4_fs_path_size(path, &info->size) : rc;
    }

    uint64_t size;
    const uint32_t result = ps2_sb4_fs_path_size(path, &size);
    if (!result) {
        info->size = size;
        return 0;
    }
    if (result == PS2_FS_BAD_REPLY) return result;
    Sb4StorageEntry entry;
    uint32_t count;
    const uint32_t listed = metadata->list(module, name, &entry, 1, &count);
    if (listed && listed != SB4_STORAGE_LIMIT) return listed;
    info->directory = 1;
    return 0;
}

uint32_t ps2_sb4_fs_list(const char *path, Sb4StorageEntry *entries, uint32_t capacity, uint32_t *count) {
    if (!count) return PS2_FS_INVALID;
    *count = 0;
    const Sb4Storage *metadata = sb4ModuleStorage(module);
    const char *name = relative_path(path, 1);
    if (!metadata || !name) return PS2_FS_INVALID;
    return metadata->list(module, name, entries, capacity, count);
}

uint32_t ps2_sb4_fs_open_dir(const char *path, uint32_t *token) {
    if (!token) return PS2_FS_INVALID;
    *token = 0;
    const char *name = relative_path(path, 1);
    if (!name) return PS2_FS_INVALID;
    const Sb4StorageDirectories *dirs = sb4StorageDirectories(sb4ModuleStorage(module));
    return dirs ? dirs->open(module, name, token) : PS2_FS_UNSUPPORTED;
}

uint32_t ps2_sb4_fs_read_dir(uint32_t token, Sb4StorageEntry *entries, uint32_t capacity, uint32_t *count) {
    if (!count) return PS2_FS_INVALID;
    *count = 0;
    const Sb4StorageDirectories *dirs = sb4StorageDirectories(sb4ModuleStorage(module));
    return dirs ? dirs->read(module, token, entries, capacity, count) : PS2_FS_UNSUPPORTED;
}

uint32_t ps2_sb4_fs_close_dir(uint32_t token) {
    const Sb4StorageDirectories *dirs = sb4StorageDirectories(sb4ModuleStorage(module));
    return dirs ? dirs->close(module, token) : PS2_FS_UNSUPPORTED;
}

uint32_t ps2_sb4_fs_ensure_directory(const char *name) {
    if (!storage || !name || (strcmp(name, "saves") && strncmp(name, "saves/", 6))) return PS2_FS_INVALID;
    const uint32_t result = storage->mkdir(module, name);
    if (result != FS_PATH_EXISTS) return result;
    const Sb4Storage *metadata = sb4ModuleStorage(module);
    Sb4StorageEntry entry;
    uint32_t count;
    const uint32_t listed = metadata->list(module, name, &entry, 1, &count);
    return listed == SB4_STORAGE_LIMIT ? 0 : listed;
}

uint32_t ps2_sb4_fs_remove_save(const char *path) {
    const char *name = relative_path(path, 0);
    if (!name || strncmp(name, "saves/", 6)) return PS2_FS_INVALID;
    const Sb4StorageFiles *files = sb4StorageFileChanges(sb4ModuleStorage(module));
    return files ? files->remove_file(module, name) : PS2_FS_UNSUPPORTED;
}

uint32_t ps2_sb4_fs_commit_saves(void) {
    return storage ? storage->commit(module) : PS2_FS_INVALID;
}

uint32_t ps2_sb4_fs_rename_save(const char *from, const char *to) {
    const char *source = relative_path(from, 0);
    const char *destination = relative_path(to, 0);
    if (!source || !destination || strncmp(source, "saves/", 6) ||
        strncmp(destination, "saves/", 6)) return PS2_FS_INVALID;
    const Sb4Storage *metadata = sb4ModuleStorage(module);
    const Sb4StorageFiles *files = sb4StorageFileChanges(metadata);
    if (!files) return PS2_FS_UNSUPPORTED;
    uint64_t existing = 0;
    uint32_t result = metadata->file_size(module, destination, &existing);
    if (!result) return FS_PATH_EXISTS;
    if (result != FS_PATH_NOT_FOUND) return result;
    return files->rename_file(module, source, destination);
}

uint32_t ps2_sb4_fs_replace_cache(const char *from, const char *to) {
    const Sb4Compat *compat = sb4ModuleCompat(module);
    if (!compat) return PS2_FS_UNSUPPORTED;
    Sb4StorageReplace publish = (Sb4StorageReplace)compat->resolve_c("sb4PublishPs2Cache");
    if (publish) return publish(module, from, to);
    static const char cache_root[] = PS2_SB4_CACHE_DIR "/";
    const char *source = relative_path(from, 0);
    const char *destination = relative_path(to, 0);
    if (!source || !destination || strncmp(source, cache_root, sizeof(cache_root) - 1) ||
        strncmp(destination, cache_root, sizeof(cache_root) - 1))
        return PS2_FS_INVALID;
    Sb4StorageReplace replace_file = (Sb4StorageReplace)compat->resolve_c("sb4ReplaceFile");
    return replace_file ? replace_file(module, source, destination) : PS2_FS_UNSUPPORTED;
}
