#ifndef IZ2_MENU_ARTWORK_H
#define IZ2_MENU_ARTWORK_H

typedef struct {
    uint32_t pending;
    uint32_t catalog_revision;
    bool presented;
} Iz2MenuArtwork;

_Static_assert(IZ_MAX_APPS <= 32, "One bit per menu slot");

static bool iz2_defer_artwork(Iz2MenuArtwork *artwork, uint32_t slot)
{
    if (!artwork || slot == 0 || slot >= IZ_MAX_APPS) return false;
    artwork->pending |= UINT32_C(1) << slot;
    return true;
}

#ifdef IZ_OUNCE_PRESENTATION
/* The metadata pass already checked the header. Read it again before using
 * its tile offset, in case an app was replaced on SD after publication. */
static bool iz2_read_tile(IzFs *fs, const Entry *entry, u8 *tile)
{
    char path[128];
    iz_fs_app_path(path, entry->folder);
    u32 file = 0;
    u64 bytes = 0;
    u32 rc = iz_fs_open(fs, path, &file, &bytes);
    if (rc) {
        __atomic_store_n(&fs->c->rescan, 1, __ATOMIC_RELEASE);
        return false;
    }

    IzAppHeader current;
    bool matches = bytes == entry->h.total_bytes &&
        !iz_fs_read(fs, file, 0, &current, sizeof current) &&
        !memcmp(&current, &entry->h, sizeof current);
    if (matches) rc = read_section(fs, file, &current.section[SEC_TILE], tile);
    if (fs->c->poisoned) return false;
    u32 close = iz_fs_close(fs, file);
    if (!matches || close || (rc && rc != 0xe110))
        __atomic_store_n(&fs->c->rescan, 1, __ATOMIC_RELEASE);

    return matches && !rc && !close && GET(tile, 102400) && GET(tile, 102400) <= 512 &&
        GET(tile, 102404) && GET(tile, 102404) <= 128;
}

/* Called by the menu thread between completed presents. An app, a page
 * replacement or a PC display hold cannot own this buffer at the same time. */
static bool iz2_menu_artwork_step(IzFs *fs, const Entry *apps,
                                  Iz2MenuArtwork *artwork, u8 *controls)
{
    IzContext *c = fs->c;
    if (!artwork->presented || !artwork->pending) return false;
    if (artwork->catalog_revision != __atomic_load_n(&c->catalog_revision, __ATOMIC_ACQUIRE)) {
        artwork->pending = 0;
        return false;
    }

    unsigned slot = GET(controls, 80);
    if (slot >= IZ_MAX_APPS || slot >= c->catalog_count ||
        !(artwork->pending & (UINT32_C(1) << slot)))
        slot = (unsigned)__builtin_ctz(artwork->pending);
    artwork->pending &= ~(UINT32_C(1) << slot);
    if (slot >= c->catalog_count) return false;

    const Entry *entry = &apps[slot];
    const size_t offset = MENU_TILES_AT + slot * IZ_TILE_BYTES;
    u8 *tile = (u8 *)(uintptr_t)c->snapshot + offset;
    if (entry->host == 3) {
        iz_ounce_apps_icon(entry->platform_token, tile + 65536, 96 * 96 * 4);
    } else if (c->arena && c->arena_bytes >= IZ_TILE_BYTES &&
               iz2_read_tile(fs, entry, (u8 *)(uintptr_t)c->arena)) {
        memcpy(tile, (const void *)(uintptr_t)c->arena, IZ_TILE_BYTES);
        W(tile, 102524, 0);
        W(tile, 102512, 0x44534453u);
        W(tile, 102516, 1);
    }
    if (c->poisoned) return false;
    if (c->platform_title) c->platform_title(tile, entry->h.name);
    memcpy(controls + offset, tile, IZ_TILE_BYTES);
    return true;
}
#endif

#endif
