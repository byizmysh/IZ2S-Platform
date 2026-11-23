#ifndef PS2_PSX_SETTINGS_MENU_H
#define PS2_PSX_SETTINGS_MENU_H

#include "ps2_sb4_preferences.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PS2_MENU_A = 1u << 0,
    PS2_MENU_B = 1u << 1,
    PS2_MENU_X = 1u << 2,
    PS2_MENU_Y = 1u << 3,
    PS2_MENU_L = 1u << 6,
    PS2_MENU_R = 1u << 7,
    PS2_MENU_ZL = 1u << 8,
    PS2_MENU_ZR = 1u << 9,
    PS2_MENU_MINUS = 1u << 11,
    PS2_MENU_LEFT = 1u << 12,
    PS2_MENU_UP = 1u << 13,
    PS2_MENU_RIGHT = 1u << 14,
    PS2_MENU_DOWN = 1u << 15
};
typedef struct {
    unsigned tab;
    unsigned row;
    unsigned player;
    unsigned confirm_reset;
    uint32_t error;
} Ps2SettingsMenu;

enum Ps2SettingsMenuAction { PS2_SETTINGS_STAY, PS2_SETTINGS_BACK, PS2_SETTINGS_BOOT_BIOS };
enum Ps2SettingsMenuAction ps2_settings_menu_step(Ps2SettingsMenu *, Ps2Settings *, uint32_t pressed);
void ps2_settings_menu_draw(uint32_t *, const Ps2SettingsMenu *, const Ps2Settings *,
                           unsigned connected_mask);

#ifdef __cplusplus
}
#endif
#endif
