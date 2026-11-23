#ifndef PS2_SB4_LIBRARY_H
#define PS2_SB4_LIBRARY_H

#include <sb4/native-app.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PS2_SB4_LIBRARY_GAMES, PS2_SB4_LIBRARY_BIOS };
enum { PS2_SB4_LIBRARY_INVALID = 0xe8e5, PS2_SB4_LIBRARY_NOT_FOUND = 0x202 };
enum { PS2_SB4_LIBRARY_ROWS = 12, PS2_SB4_FRAME_WIDTH = 960, PS2_SB4_FRAME_HEIGHT = 540 };
enum { PS2_SB4_MEDIA_PATH_MAX = 256 };

typedef struct {
    char bios_filename[PS2_SB4_MEDIA_PATH_MAX];
    char iso[PS2_SB4_MEDIA_PATH_MAX]; /* Empty for BIOS-only mode. */
} Ps2Sb4Media;

typedef struct {
    const Sb4Compat *compat;
    Sb4StorageEntry *entries;
    uint32_t count, category;
    char directory[PS2_SB4_MEDIA_PATH_MAX]; /* Relative to games/. */
} Ps2Sb4Library;

/* Start with zeroed storage. A successful refresh replaces the previous list.
 * Read a selected entry's name from entries[index]. Close before app exit. */
uint32_t ps2_sb4_library_refresh(Ps2Sb4Library *, IzNativeApp *, uint32_t category);
uint32_t ps2_sb4_library_open_games(Ps2Sb4Library *, IzNativeApp *, const char *directory);
uint32_t ps2_sb4_library_game_path(const Ps2Sb4Library *, const char *name,
                                  char path[PS2_SB4_MEDIA_PATH_MAX]);
void ps2_sb4_library_close(Ps2Sb4Library *);
/* Framebuffer publication for the library and its settings. The launcher owns the queue. */
uint32_t *ps2_sb4_menu_pixels(IzNativeApp *);
void ps2_sb4_menu_commit(IzNativeApp *);
uint32_t ps2_sb4_library_draw(const Ps2Sb4Library *, IzNativeApp *, uint32_t selected,
                              uint32_t top, const char *message);
uint32_t ps2_sb4_loading_draw(IzNativeApp *, const char *message, uint32_t percent);
/* Validate the media in the module's SD folders and copy the core's paths.
 * An empty disc filename means BIOS-only. */
uint32_t ps2_sb4_media_prepare(IzNativeApp *, const char *bios, const char *disc, Ps2Sb4Media *);

#ifdef __cplusplus
}
#endif
#endif
