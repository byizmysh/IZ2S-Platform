#ifndef PS2_SB4_PREFERENCES_H
#define PS2_SB4_PREFERENCES_H

#include "ps2_controller_settings.h"
#include "ps2_frame_limit.h"
#include "ps2_sb4_library.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t aspect; /* Default, 4:3, 16:9, Stretch. */
    uint32_t volume; /* 0..100. */
    uint32_t full_boot;
    uint32_t multitap;
    uint32_t devices[PS2_PAD_PLAYERS];
    Ps2PadProfile profiles[PS2_PAD_PLAYERS];
    char last_game[PS2_SB4_MEDIA_PATH_MAX];
    uint32_t frame_limit; /* enum Ps2FrameLimit */
} Ps2Preferences;

typedef struct {
    Ps2Preferences prefs;
    uint64_t generation;
    uint32_t active;
    uint32_t dirty;
    uint32_t warning;
} Ps2Settings;

enum { PS2_SETTINGS_INVALID = 0xe8eb };

uint32_t ps2_settings_load(Ps2Settings *, IzNativeApp *);
uint32_t ps2_settings_save(Ps2Settings *, IzNativeApp *);

#ifdef __cplusplus
}
#endif
#endif
