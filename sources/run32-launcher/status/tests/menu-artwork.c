/* Menu-thread regression test using the same artwork code as the launcher. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IZ_OUNCE_PRESENTATION 1
#define IZ_MAX_APPS 4
#define IZ_TILE_BYTES 131072
#define MENU_TILES_AT 1024

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u32 offset, bytes, crc32; } IzSection;
enum { SEC_TILE };
typedef struct { u32 total_bytes; char name[64]; IzSection section[1]; } IzAppHeader;
typedef struct { IzAppHeader h; char folder[48]; u32 host, platform_token; } Entry;
typedef struct {
    uintptr_t snapshot, arena;
    u32 arena_bytes, poisoned, rescan, catalog_count, catalog_revision;
    void (*platform_title)(void *, const char *);
} IzContext;
typedef struct { IzContext *c; } IzFs;

#define GET(p,o) (*(u32 *)((u8 *)(p) + (o)))
#define W(p,o,v) (GET((p),(o)) = (v))

static unsigned icon_reads, opens, headers, tile_reads, titles, closes;
static uint8_t last_icon;
static bool fail_crc;
static IzAppHeader sd_header;
static _Alignas(4) u8 snapshot[MENU_TILES_AT + IZ_MAX_APPS * IZ_TILE_BYTES];
static _Alignas(4) u8 controls[sizeof snapshot], scratch[IZ_TILE_BYTES];

static void iz_fs_app_path(char *path, const char *folder)
{
    assert(!strcmp(folder, "example"));
    strcpy(path, "example/launcher.iza");
}

static u32 iz_fs_open(IzFs *fs, const char *path, u32 *file, u64 *bytes)
{
    (void)fs;
    assert(!strcmp(path, "example/launcher.iza"));
    ++opens;
    *file = 1;
    *bytes = sd_header.total_bytes;
    return 0;
}

static u32 iz_fs_read(IzFs *fs, u32 file, u64 offset, void *data, u32 size)
{
    (void)fs;
    assert(file == 1 && offset == 0 && size == sizeof sd_header);
    ++headers;
    memcpy(data, &sd_header, size);
    return 0;
}

static u32 iz_fs_close(IzFs *fs, u32 file)
{
    (void)fs;
    assert(file == 1);
    ++closes;
    return 0;
}

static u32 read_section(IzFs *fs, u32 file, const IzSection *section, void *tile)
{
    (void)fs;
    assert(file == 1 && section->bytes == IZ_TILE_BYTES);
    ++tile_reads;
    memset(tile, 0, IZ_TILE_BYTES);
    W(tile, 102400, 100);
    W(tile, 102404, 40);
    ((u8 *)tile)[65536] = 42;
    return fail_crc ? 0xe110 : 0;
}

static bool iz_ounce_apps_icon(u32 token, void *image, size_t bytes)
{
    assert(bytes == 96 * 96 * 4);
    ++icon_reads;
    last_icon = (u8)token;
    ((u8 *)image)[0] = last_icon;
    return true;
}

static void subtitle(void *tile, const char *name)
{
    assert(name[0]);
    ++titles;
    ((u8 *)tile)[0] = (u8)name[0];
}

#include "menu-artwork.h"

int main(void)
{
    IzContext c = {
        .snapshot = (uintptr_t)snapshot, .arena = (uintptr_t)scratch,
        .arena_bytes = sizeof scratch, .catalog_count = 4, .catalog_revision = 2,
        .platform_title = subtitle,
    };
    IzFs fs = {&c};
    Entry apps[IZ_MAX_APPS] = {
        { .h.name = "Eager" },
        { .h.name = "NRO", .host = 3, .platform_token = 10 },
        { .h = { .name = "IZA", .total_bytes = sizeof(IzAppHeader) + IZ_TILE_BYTES,
                  .section = {{sizeof(IzAppHeader), IZ_TILE_BYTES, 7}}},
          .folder = "example" },
        { .h.name = "Selected", .host = 3, .platform_token = 12 },
    };
    sd_header = apps[2].h;
    Iz2MenuArtwork work = {.catalog_revision = c.catalog_revision};
    assert(!iz2_defer_artwork(NULL, 1));
    assert(!iz2_defer_artwork(&work, 0));
    for (u32 i = 1; i < IZ_MAX_APPS; ++i) assert(iz2_defer_artwork(&work, i));
    W(controls, 80, 3);
    assert(!iz2_menu_artwork_step(&fs, apps, &work, controls));
    assert(!icon_reads && !tile_reads && !titles);

    work.presented = true;
    assert(iz2_menu_artwork_step(&fs, apps, &work, controls));
    assert(icon_reads == 1 && last_icon == 12 && !tile_reads && GET(controls, 80) == 3);
    assert(iz2_menu_artwork_step(&fs, apps, &work, controls));
    assert(icon_reads == 2 && last_icon == 10 && !tile_reads);
    assert(iz2_menu_artwork_step(&fs, apps, &work, controls));
    size_t offset = MENU_TILES_AT + 2 * IZ_TILE_BYTES;
    assert(opens == 1 && headers == 1 && tile_reads == 1 && closes == 1 && titles == 3);
    assert(controls[offset + 65536] == 42 && !memcmp(controls + offset, snapshot + offset, IZ_TILE_BYTES));
    assert(!iz2_menu_artwork_step(&fs, apps, &work, controls));

    /* A thumbnail from an updated package or damaged section is never displayed. */
    snapshot[offset + 65536] = 99;
    ++sd_header.section[0].crc32;
    assert(iz2_defer_artwork(&work, 2));
    assert(iz2_menu_artwork_step(&fs, apps, &work, controls));
    assert(tile_reads == 1 && closes == 2 && controls[offset + 65536] == 99 && c.rescan);
    c.rescan = 0;
    sd_header = apps[2].h;
    fail_crc = true;
    assert(iz2_defer_artwork(&work, 2));
    assert(iz2_menu_artwork_step(&fs, apps, &work, controls));
    assert(tile_reads == 2 && closes == 3 && controls[offset + 65536] == 99 && !c.rescan);

    assert(iz2_defer_artwork(&work, 1));
    c.catalog_revision += 2;
    assert(!iz2_menu_artwork_step(&fs, apps, &work, controls));
    assert(!work.pending && icon_reads == 2 && tile_reads == 2);
    puts("PASS: eager first tile, no artwork before first present, selection priority, one tile per step, CRC and stale catalog rejection");
}
