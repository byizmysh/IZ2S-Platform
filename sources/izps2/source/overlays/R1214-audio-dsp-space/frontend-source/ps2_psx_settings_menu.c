#include "ps2_psx_settings_menu.h"
#include "ps2_psx_ui.h"

enum { VIDEO, AUDIO, EMULATION, CONTROLS, SYSTEM, ABOUT, TABS };
static const char tabs[TABS][12] = {"VIDEO", "AUDIO", "EMULATION", "CONTROLS", "SYSTEM", "ABOUT"};
static const char aspects[][8] = {"AUTO", "4:3", "16:9", "STRETCH"};
static const char frame_limits[PS2_FRAME_LIMIT_COUNT][8] = {
    "OFF", "30 FPS", "40 FPS", "50 FPS", "60 FPS"
};
static const char boots[][16] = {"FAST BOOT", "FULL BIOS BOOT"};
static const char multitaps[][24] = {"OFF - 2 PLAYERS", "PORT 1 - 4 PLAYERS", "PORT 2 - 4 PLAYERS"};
static const char devices[][16] = {
    "AUTO", "OFF", "CONTROLLER 1", "CONTROLLER 2", "CONTROLLER 3", "CONTROLLER 4",
    "CONTROLLER 5", "CONTROLLER 6", "CONTROLLER 7", "CONTROLLER 8", "HANDHELD"
};
static const char toggles[][4] = {"OFF", "ON"};
static const char deadzones[][8] = {"SMALL", "MEDIUM", "LARGE"};
static const char turbo[][8] = {"OFF", "5 HZ", "10 HZ", "15 HZ"};
static const uint32_t white = 0xfff5f2ec, grey = 0xffb3a79b, accent = 0xffeac45c;
static const uint32_t highlight = 0xff493623;

static unsigned wrap(unsigned current, unsigned count, int direction) {
    return (current + (direction < 0 ? count - 1 : 1)) % count;
}

static void edit(Ps2SettingsMenu *menu, Ps2Settings *settings, int direction) {
    Ps2Preferences *prefs = &settings->prefs;
    uint32_t *setting;
    unsigned count, reset = 0;
    switch (menu->tab) {
    case VIDEO:
        if (menu->row == 0) {
            setting = &prefs->aspect;
            count = 4;
        } else {
            setting = &prefs->frame_limit;
            count = PS2_FRAME_LIMIT_COUNT;
        }
        break;
    case AUDIO: setting = &prefs->volume; count = 101; reset = 100; break;
    case EMULATION: setting = &prefs->full_boot; count = 2; break;
    case CONTROLS:
        if (menu->row == 0) {
            menu->player = direction ? wrap(menu->player, PS2_PAD_PLAYERS, direction) : 0;
            return;
        }
        switch (menu->row) {
        case 1: setting = &prefs->multitap; count = 3; break;
        case 2: setting = &prefs->devices[menu->player]; count = PS2_PAD_DEVICE_COUNT; break;
        case 3: setting = &prefs->profiles[menu->player].swap_ab; count = 2; break;
        case 4: setting = &prefs->profiles[menu->player].stick_dpad; count = 2; break;
        case 5: setting = &prefs->profiles[menu->player].deadzone; count = 3; reset = 1; break;
        case 6: setting = &prefs->profiles[menu->player].turbo_cross; count = 4; break;
        default: return;
        }
        break;
    default: return;
    }
    const unsigned value = direction ? wrap(*setting, count, direction) : reset;
    if (value != *setting) {
        *setting = value;
        settings->dirty = 1;
    }
}

enum Ps2SettingsMenuAction ps2_settings_menu_step(Ps2SettingsMenu *menu, Ps2Settings *settings,
                                                  uint32_t pressed) {
    if (menu->confirm_reset) {
        if (pressed & PS2_MENU_A) {
            if (menu->tab == CONTROLS) {
                settings->prefs.devices[menu->player] = PS2_PAD_DEVICE_AUTO;
                settings->prefs.profiles[menu->player] = (Ps2PadProfile){.deadzone = 1};
                settings->dirty = 1;
            } else if (menu->tab == VIDEO) {
                settings->prefs.aspect = 0;
                settings->prefs.frame_limit = PS2_FRAME_LIMIT_OFF;
                settings->dirty = 1;
            } else edit(menu, settings, 0);
        }
        if (pressed & (PS2_MENU_A | PS2_MENU_B | PS2_MENU_MINUS)) menu->confirm_reset = 0;
        return PS2_SETTINGS_STAY;
    }
    if (pressed & (PS2_MENU_B | PS2_MENU_MINUS)) return PS2_SETTINGS_BACK;
    if (pressed & (PS2_MENU_L | PS2_MENU_R)) {
        menu->tab = wrap(menu->tab, TABS, pressed & PS2_MENU_L ? -1 : 1);
        menu->row = 0;
    }
    if (menu->tab == CONTROLS) {
        if (pressed & (PS2_MENU_ZL | PS2_MENU_ZR))
            menu->player = wrap(menu->player, PS2_PAD_PLAYERS, pressed & PS2_MENU_ZL ? -1 : 1);
        if (pressed & (PS2_MENU_UP | PS2_MENU_DOWN))
            menu->row = wrap(menu->row, 7, pressed & PS2_MENU_UP ? -1 : 1);
    }
    if (menu->tab == VIDEO && (pressed & (PS2_MENU_UP | PS2_MENU_DOWN)))
        menu->row ^= 1;
    if (menu->tab == SYSTEM && (pressed & PS2_MENU_A)) return PS2_SETTINGS_BOOT_BIOS;
    if (pressed & PS2_MENU_LEFT) edit(menu, settings, -1);
    else if (pressed & (PS2_MENU_RIGHT | PS2_MENU_A)) edit(menu, settings, 1);
    if (pressed & PS2_MENU_X) edit(menu, settings, 0);
    if ((pressed & PS2_MENU_Y) && menu->tab < SYSTEM) menu->confirm_reset = 1;
    return PS2_SETTINGS_STAY;
}

static void row(uint32_t *pixels, unsigned y, const char *label, const char *value, int selected) {
    if (selected) {
        ps2_ui_rect(pixels, 22, y - 5, 596, 24, highlight);
        ps2_ui_rect(pixels, 22, y - 5, 3, 24, accent);
    }
    ps2_ui_text(pixels, 36, y, label, 1, selected ? white : grey, 46);
    ps2_ui_text(pixels, 438, y, value, 1, selected ? accent : grey, 25);
}

void ps2_settings_menu_draw(uint32_t *pixels, const Ps2SettingsMenu *menu,
                           const Ps2Settings *settings, unsigned connected_mask) {
    const Ps2Preferences *pref = &settings->prefs;
    ps2_ui_chrome(pixels, "SETTINGS - MAKE IT YOURS");
    for (unsigned tab = 0; tab < TABS; ++tab) {
        const unsigned x = 24 + tab * 100;
        if (tab == menu->tab) {
            ps2_ui_rect(pixels, x, 86, 94, 25, highlight);
            ps2_ui_rect(pixels, x, 109, 94, 2, accent);
        }
        ps2_ui_text(pixels, x + 6, 95, tabs[tab], 1, tab == menu->tab ? white : grey, 14);
    }
    if (menu->tab == CONTROLS) {
        for (unsigned i = 0; i < PS2_PAD_PLAYERS; ++i) {
            const unsigned x = 24 + i * 150;
            const int selected = i == menu->player;
            const int connected = connected_mask != UINT32_MAX && (connected_mask & (1u << i)) != 0;
            char name[] = "PLAYER 1";
            name[7] += i;
            ps2_ui_rect(pixels, x, 123, 144, 45, selected ? highlight : 0xff29221c);
            if (selected) ps2_ui_rect(pixels, x, 165, 144, 3, accent);
            if (selected && !menu->row) ps2_ui_rect(pixels, x, 123, 3, 45, accent);
            ps2_ui_text(pixels, x + 10, 131, name, 1, selected ? white : grey, 16);
            ps2_ui_text(pixels, x + 10, 150,
                        pref->devices[i] == PS2_PAD_DEVICE_OFF ? "DISABLED" :
                        connected ? "CONNECTED" : connected_mask == UINT32_MAX ? "READY" : "NOT CONNECTED", 1, connected ? accent : grey, 18);
        }
        row(pixels, 201, "Multitap (next game)", multitaps[pref->multitap], menu->row == 1);
        row(pixels, 226, "Input device", devices[pref->devices[menu->player]], menu->row == 2);
        const Ps2PadProfile *profile = &pref->profiles[menu->player];
        row(pixels, 251, "Swap A / B", toggles[profile->swap_ab], menu->row == 3);
        row(pixels, 276, "Stick as D-pad", toggles[profile->stick_dpad], menu->row == 4);
        row(pixels, 301, "Stick deadzone", deadzones[profile->deadzone], menu->row == 5);
        row(pixels, 326, "Turbo cross", turbo[profile->turbo_cross], menu->row == 6);
        ps2_ui_text(pixels, 30, 378, "Choose Port 1 or Port 2 for games with Multitap support.",
                    1, grey, 84);
    } else if (menu->tab == VIDEO) {
        row(pixels, 130, "Aspect ratio", aspects[pref->aspect], menu->row == 0);
        row(pixels, 155, "FPS limit", frame_limits[pref->frame_limit], menu->row == 1);
    } else if (menu->tab == AUDIO) {
        const unsigned volume = pref->volume;
        char value[] = "100%";
        unsigned start = volume == 100 ? 0 : volume >= 10 ? 1 : 2;
        if (volume < 100) {
            value[1] = '0' + volume / 10;
            value[2] = '0' + volume % 10;
        }
        row(pixels, 130, "Volume", value + start, 1);
    } else if (menu->tab == EMULATION) {
        row(pixels, 130, "Boot method", boots[pref->full_boot], 1);
    } else if (menu->tab == SYSTEM) {
        row(pixels, 130, "Boot PlayStation 2 BIOS", "A OPEN", 1);
    } else {
        ps2_ui_logo(pixels, 36, 138, 2);
        ps2_ui_text(pixels, 252, 146, "IZPS2", 4, white, 8);
        ps2_ui_text(pixels, 254, 190, "@byizmysh", 1, accent, 24);
    }
    if (menu->tab == VIDEO)
        ps2_ui_text(pixels, 30, 398, "ASPECT RATIO APPLIES NEXT GAME LAUNCH.", 1, accent, 84);
    else if (menu->tab != SYSTEM && menu->tab != ABOUT)
        ps2_ui_text(pixels, 30, 398, "NEXT GAME LAUNCH - CHOOSE THE GAME AGAIN TO APPLY.",
                    1, accent, 84);
    const char *status = menu->error ? "Settings could not be saved. Press B to try again." :
        menu->confirm_reset ? "Reset settings? A confirms / B cancels." :
        settings->warning ? "An older settings copy was recovered. Check these values." :
        settings->dirty ? "B SAVES YOUR CHANGES AND GOES BACK." : "B BACK";
    ps2_ui_text(pixels, 24, 432, status, 1, menu->error ? 0xffb4b8ff : accent, 85);
    ps2_ui_text(pixels, 24, 455, menu->tab == CONTROLS ?
        "L/R TABS   ZL/ZR PLAYER   LEFT/RIGHT EDIT   X DEFAULT   Y RESET" :
        menu->tab == VIDEO ? "L/R TABS   UP/DOWN SELECT   LEFT/RIGHT EDIT   X DEFAULT   Y RESET" :
        "L/R TABS   LEFT/RIGHT ADJUST   X DEFAULT   Y RESET TAB",
        1, grey, 84);
}
