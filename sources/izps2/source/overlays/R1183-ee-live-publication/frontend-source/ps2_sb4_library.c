#include "ps2_sb4_layout.h"
#include "ps2_sb4_library.h"
#include "ps2_psx_ui.h"
#include "native_video.h"
#include <string.h>

enum { BIOS_BYTES = 4 * 1024 * 1024 };
typedef void (*SortEntries)(void *, size_t, size_t, int (*)(const void *, const void *));
static const char bios_directory[] = "bios/";
static const char games_directory[] = "games/";
static const char disc_root[] = PS2_SB4_SDK_ROOT "/games/";
enum { DISC_NAME_CAPACITY = PS2_SB4_MEDIA_PATH_MAX - (sizeof(disc_root) - 1) };

static size_t path_bytes(const char *name, int nested) {
    if (!name) return 0;
    int start = 1;
    for (size_t i = 0; i < PS2_SB4_MEDIA_PATH_MAX; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!ch) return start ? 0 : i;
        if (ch == '/') {
            if (!nested || start) return 0;
            start = 1;
            continue;
        }
        if (ch < 32 || ch == 127 || ch == '\\' || ch == ':' || (start && ch == '.')) return 0;
        start = 0;
    }
    return 0;
}

static unsigned lower_ascii(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

static int compare_names(const char *a, const char *b, int ignore_case) {
    for (; *a || *b; ++a, ++b) {
        unsigned x = ignore_case ? lower_ascii(*a) : (unsigned char)*a;
        unsigned y = ignore_case ? lower_ascii(*b) : (unsigned char)*b;
        if (x != y) return (int)x - (int)y;
    }
    return 0;
}

static size_t logical_disc_bytes(const char *name, size_t bytes) {
    static const char first_part[] = ".part000";
    const size_t suffix_bytes = sizeof first_part - 1;
    return bytes > suffix_bytes && !memcmp(name + bytes - suffix_bytes, first_part, suffix_bytes)
        ? bytes - suffix_bytes : bytes;
}

static int media_file(size_t bytes, const char *name, uint32_t category, uint64_t size) {
    if (!bytes) return 0;
    if (category == PS2_SB4_LIBRARY_BIOS) return size == BIOS_BYTES;
    if (!size || bytes >= DISC_NAME_CAPACITY)
        return 0;
    bytes = logical_disc_bytes(name, bytes);
    size_t suffix = bytes;
    while (suffix && name[suffix - 1] != '.') --suffix;
    if (!suffix) return 0;
    char extension[6] = {0};
    const size_t extension_bytes = bytes - suffix + 1;
    if (extension_bytes >= sizeof extension) return 0;
    memcpy(extension, name + suffix - 1, extension_bytes);
    // VMManager disc formats, the CDVD block dump reader and ELF boot.
    static const char extensions[][6] = {
        ".iso", ".bin", ".img", ".mdf", ".gz", ".cso", ".zso", ".chd", ".dump", ".elf",
    };
    for (size_t i = 0; i < sizeof extensions / sizeof *extensions; ++i)
        if (!compare_names(extension, extensions[i], 1)) return 1;
    return 0;
}

static int by_name(const void *left, const void *right) {
    const Sb4StorageEntry *a = left, *b = right;
    if (!!a->directory != !!b->directory) return a->directory ? 1 : -1;
    int order = compare_names(a->name, b->name, 1);
    return order ? order : compare_names(a->name, b->name, 0);
}

static int eligible_media(const Ps2Sb4Library *library, const Sb4StorageEntry *entry) {
    const size_t bytes = path_bytes(entry->name, 0);
    if (!bytes) return 0;
    if (library->category == PS2_SB4_LIBRARY_BIOS)
        return !entry->directory && media_file(bytes, entry->name, library->category, entry->size);
    const size_t parent = strlen(library->directory);
    if (bytes + parent + (parent != 0) >= DISC_NAME_CAPACITY) return 0;
    return entry->directory || media_file(bytes, entry->name, library->category, entry->size);
}

uint32_t ps2_sb4_library_game_path(const Ps2Sb4Library *library, const char *name,
                                  char path[PS2_SB4_MEDIA_PATH_MAX]) {
    const size_t bytes = path_bytes(name, 0);
    if (!library || !bytes || !path) return PS2_SB4_LIBRARY_INVALID;
    size_t parent = strlen(library->directory);
    if (bytes + parent + (parent != 0) >= DISC_NAME_CAPACITY) return PS2_SB4_LIBRARY_INVALID;
    if (parent) {
        memcpy(path, library->directory, parent);
        path[parent++] = '/';
    }
    memcpy(path + parent, name, bytes + 1);
    return 0;
}

static uint32_t scan_pages(Ps2Sb4Library *library, IzNativeApp *app,
                           const Sb4StorageDirectories *dirs, const char *name) {
    uint32_t token = 0;
    uint32_t rc = dirs->open(app, name, &token);
    if (rc == PS2_SB4_LIBRARY_NOT_FOUND && !library->directory[0]) return 0;
    if (rc || !token) return rc ? rc : PS2_SB4_LIBRARY_INVALID;

    const Sb4Compat *compat = library->compat;
    size_t capacity = 0;
    Sb4StorageEntry page[SB4_STORAGE_PAGE_MAX];
    while (!rc) {
        uint32_t count = 0;
        rc = dirs->read(app, token, page, SB4_STORAGE_PAGE_MAX, &count);
        if (rc || !count) break;
        if (count > SB4_STORAGE_PAGE_MAX) { rc = PS2_SB4_LIBRARY_INVALID; break; }
        for (uint32_t i = 0; i < count; ++i) {
            if (!eligible_media(library, &page[i])) continue;
            if (library->count == capacity) {
                if (capacity > UINT32_MAX / 2 || capacity > SIZE_MAX / sizeof(*page) / 2) {
                    rc = SB4_STORAGE_LIMIT;
                    break;
                }
                const size_t next = capacity ? capacity * 2 : SB4_STORAGE_PAGE_MAX;
                void *memory = NULL;
                rc = compat->allocate_aligned(&memory, 16, next * sizeof(*page));
                if (rc || !memory) { if (!rc) rc = PS2_SB4_LIBRARY_INVALID; break; }
                if (library->entries) {
                    memcpy(memory, library->entries, library->count * sizeof(*page));
                    compat->release(library->entries);
                }
                library->entries = memory;
                capacity = next;
            }
            library->entries[library->count++] = page[i];
        }
    }
    const uint32_t closed = dirs->close(app, token);
    return rc ? rc : closed;
}

static uint32_t refresh_at(Ps2Sb4Library *library, IzNativeApp *app, uint32_t category,
                           const char *directory) {
    const Sb4Compat *compat = sb4ModuleCompat(app);
    const Sb4Storage *storage = sb4ModuleStorage(app);
    if (!library || !compat || !storage || !directory || category > PS2_SB4_LIBRARY_BIOS ||
        (library->compat && library->compat != compat)) return PS2_SB4_LIBRARY_INVALID;
    SortEntries sort = (SortEntries)compat->resolve_c("qsort");
    if (!sort) return PS2_SB4_LIBRARY_INVALID;
    Ps2Sb4Library candidate = {.compat = compat, .category = category};
    char sd_path[sizeof(games_directory) + PS2_SB4_MEDIA_PATH_MAX] = "games";
    const char *name = category == PS2_SB4_LIBRARY_BIOS ? "bios" : sd_path;
    if (*directory) {
        const size_t bytes = path_bytes(directory, 1);
        if (!bytes || bytes >= DISC_NAME_CAPACITY || category != PS2_SB4_LIBRARY_GAMES)
            return PS2_SB4_LIBRARY_INVALID;
        memcpy(candidate.directory, directory, bytes + 1);
        memcpy(sd_path, games_directory, sizeof games_directory - 1);
        memcpy(sd_path + sizeof games_directory - 1, directory, bytes + 1);
    }
    const Sb4StorageDirectories *dirs = sb4StorageDirectories(storage);
    uint32_t rc;
    if (dirs) {
        rc = scan_pages(&candidate, app, dirs, name);
    } else {
        void *memory = NULL;
        rc = compat->allocate_aligned(&memory, 16, SB4_STORAGE_LIST_MAX * sizeof(Sb4StorageEntry));
        if (rc || !memory) return rc ? rc : PS2_SB4_LIBRARY_INVALID;
        candidate.entries = memory;
        uint32_t count = 0;
        rc = storage->list(app, name, candidate.entries, SB4_STORAGE_LIST_MAX, &count);
        if (!rc && count > SB4_STORAGE_LIST_MAX) rc = PS2_SB4_LIBRARY_INVALID;
        if (rc == PS2_SB4_LIBRARY_NOT_FOUND && !*directory) { rc = 0; count = 0; }
        for (uint32_t i = 0; !rc && i < count; ++i) {
            const Sb4StorageEntry *entry = &candidate.entries[i];
            if (eligible_media(&candidate, entry)) candidate.entries[candidate.count++] = *entry;
        }
    }
    if (rc) { ps2_sb4_library_close(&candidate); return rc; }
    if (candidate.count > 1) sort(candidate.entries, candidate.count, sizeof(*candidate.entries), by_name);
    ps2_sb4_library_close(library);
    *library = candidate;
    return 0;
}

uint32_t ps2_sb4_library_refresh(Ps2Sb4Library *library, IzNativeApp *app, uint32_t category) {
    return refresh_at(library, app, category,
                      library && library->category == category ? library->directory : "");
}

uint32_t ps2_sb4_library_open_games(Ps2Sb4Library *library, IzNativeApp *app, const char *directory) {
    return refresh_at(library, app, PS2_SB4_LIBRARY_GAMES, directory);
}

void ps2_sb4_library_close(Ps2Sb4Library *library) {
    if (!library || !library->compat) return;
    if (library->entries)
        library->compat->release(library->entries);
    *library = (Ps2Sb4Library){0};
}

uint32_t *ps2_sb4_menu_pixels(IzNativeApp *app) {
    if (!sb4ModuleCompat(app)) return NULL;
    const IzNativeApp2 *native = (const IzNativeApp2 *)app;
    const size_t bytes = PS2_UI_WIDTH * PS2_UI_HEIGHT * sizeof(uint32_t);
    if (!native->private_memory || (uintptr_t)native->private_memory % 4 ||
        native->private_bytes < PS2_VIDEO_HEADER_BYTES + bytes) return NULL;
    return (uint32_t *)((unsigned char *)native->private_memory + PS2_VIDEO_HEADER_BYTES);
}

void ps2_sb4_menu_commit(IzNativeApp *app) {
    uint32_t *header = ((const IzNativeApp2 *)app)->private_memory;
    header[0] = PS2_UI_WIDTH;
    header[1] = PS2_UI_HEIGHT;
    ++header[2];
    header[3] = 1;
    header[4] = header[5] = header[7] = 0;
    header[6] = 100;
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

uint32_t ps2_sb4_library_draw(const Ps2Sb4Library *library, IzNativeApp *app, uint32_t selected,
                              uint32_t top, const char *message) {
    if (!library || library->compat != sb4ModuleCompat(app) || (!library->entries && library->count) ||
        library->category != PS2_SB4_LIBRARY_GAMES || top > library->count) return PS2_SB4_LIBRARY_INVALID;
    uint32_t *pixels = ps2_sb4_menu_pixels(app);
    if (!pixels) return PS2_SB4_LIBRARY_INVALID;
    const uint32_t accent = 0xffeac45c, grey = 0xffb3a79b;
    ps2_ui_chrome(pixels, "YOUR PLAYSTATION 2 LIBRARY");
    char path[sizeof(PS2_SB4_SD_ROOT "/games/") + PS2_SB4_MEDIA_PATH_MAX] = PS2_SB4_SD_ROOT "/games";
    if (library->directory[0]) {
        size_t prefix = strlen(path);
        path[prefix++] = '/';
        memcpy(path + prefix, library->directory, strlen(library->directory) + 1);
    }
    const size_t length = strlen(path);
    ps2_ui_text(pixels, 24, 90, "SD", 1, accent, 2);
    ps2_ui_text(pixels, 48, 90, length > 79 ? path + length - 79 : path, 1, grey, 79);
    if (!library->count) {
        ps2_ui_text(pixels, 30, 155, "YOUR LIBRARY STARTS HERE", 2, 0xfff5f2ec, 45);
        ps2_ui_text(pixels, 30, 199, "ADD ISO OR CHD TO THE GAMES FOLDER", 1, grey, 79);
    }
    for (uint32_t i = top; i < library->count && i - top < PS2_SB4_LIBRARY_ROWS; ++i) {
        const unsigned y = 114 + (i - top) * 24;
        const int active = i == selected;
        const Sb4StorageEntry *entry = &library->entries[i];
        if (active) {
            ps2_ui_rect(pixels, 22, y - 5, 596, 23, 0xff493623);
            ps2_ui_rect(pixels, 22, y - 5, 3, 23, accent);
        }
        ps2_ui_text(pixels, 34, y, entry->directory ? "+" : ">", 2, active ? accent : grey, 1);
        ps2_ui_text(pixels, 62, y, entry->name, 2, active ? 0xfff5f2ec : 0xffc5b8ab, 45);
    }
    ps2_ui_text(pixels, 24, 432, message ? message : "A OPEN    B BACK    D-PAD SELECT / PAGE",
                1, 0xffe6d5c4, 85);
    ps2_ui_text(pixels, 24, 455, "X RESCAN", 1, grey, 10);
    ps2_ui_text(pixels, 278, 455, "Y LAST PLAYED / - SETTINGS", 1, grey, 45);
    ps2_sb4_menu_commit(app);
    return 0;
}

uint32_t ps2_sb4_loading_draw(IzNativeApp *app, const char *message, uint32_t percent) {
    uint32_t *pixels = ps2_sb4_menu_pixels(app);
    if (!pixels) return PS2_SB4_LIBRARY_INVALID;
    if (percent > 100) percent = 100;
    ps2_ui_chrome(pixels, "PLAYSTATION 2 ON SWITCH 2");
    ps2_ui_text(pixels, 38, 174, "GETTING READY", 3, 0xfff5f2ec, 30);
    ps2_ui_text(pixels, 40, 224, message ? message : "LOADING FROM SD", 1, 0xffccbcb0, 79);
    ps2_ui_rect(pixels, 40, 269, 560, 8, 0xff493623);
    ps2_ui_rect(pixels, 40, 269, 560 * percent / 100, 8, 0xffeac45c);
    ps2_ui_text(pixels, 24, 441, "B CANCEL", 1, 0xffb3a79b, 10);
    ps2_sb4_menu_commit(app);
    return 0;
}
uint32_t ps2_sb4_media_prepare(IzNativeApp *app, const char *bios, const char *disc, Ps2Sb4Media *out) {
    const Sb4Storage *storage = sb4ModuleStorage(app);
    if (!out) return PS2_SB4_LIBRARY_INVALID;
    *out = (Ps2Sb4Media){0};
    const size_t bios_bytes = path_bytes(bios, 0);
    if (!storage || !bios_bytes || !disc) return PS2_SB4_LIBRARY_INVALID;

    char path[sizeof(bios_directory) + sizeof(out->bios_filename)];
    uint64_t size = 0;
    memcpy(path, bios_directory, sizeof(bios_directory) - 1);
    memcpy(path + sizeof(bios_directory) - 1, bios, bios_bytes + 1);
    uint32_t rc = storage->file_size(app, path, &size);
    if (rc || !media_file(bios_bytes, bios, PS2_SB4_LIBRARY_BIOS, size))
        return rc ? rc : PS2_SB4_LIBRARY_INVALID;

    if (*disc) {
        const size_t disc_bytes = path_bytes(disc, 1);
        if (!media_file(disc_bytes, disc, PS2_SB4_LIBRARY_GAMES, 1)) return PS2_SB4_LIBRARY_INVALID;
        memcpy(path, games_directory, sizeof(games_directory) - 1);
        memcpy(path + sizeof(games_directory) - 1, disc, disc_bytes + 1);
        rc = storage->file_size(app, path, &size);
        if (rc || !size) return rc ? rc : PS2_SB4_LIBRARY_INVALID;
        const size_t logical_bytes = logical_disc_bytes(disc, disc_bytes);
        memcpy(out->iso, disc_root, sizeof(disc_root) - 1);
        memcpy(out->iso + sizeof(disc_root) - 1, disc, logical_bytes);
        out->iso[sizeof(disc_root) - 1 + logical_bytes] = '\0';
    }
    memcpy(out->bios_filename, bios, bios_bytes + 1);
    return 0;
}
