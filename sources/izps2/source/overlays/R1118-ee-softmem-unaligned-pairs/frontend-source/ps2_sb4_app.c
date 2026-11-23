#include "ps2_sb4_session.h"
#include "ps2_psx_settings_menu.h"
#include <sb4/host-posix.h>
#include <sb4/services.h>
#include <string.h>

/* The native.initial starts with the asset's 77-character name and
 * its terminating NUL. The launcher owns the zeroed app->data allocation. */
typedef struct {
    char engine_asset[80];
    Ps2Sb4Session session;
    Ps2Sb4Worker worker;
    Ps2Sb4Library games;
    Ps2Settings settings;
    Ps2SettingsMenu settings_menu;
    uint32_t settings_open;
    uint32_t restore_pad_styles, original_pad_styles;
    void *audio;
    uint64_t previous_buttons;
    uint32_t selected, top;
    uint32_t hosted_posix;
    char bios[256];
    char current_game[PS2_SB4_MEDIA_PATH_MAX];
} Ps2Sb4App;

_Static_assert(sizeof(Ps2Sb4App) <= 524288, "IZPS2 native data budget");

enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 6, PAD_R = 1 << 7,
    PAD_MINUS = 1 << 11, PAD_LEFT = (1 << 12) | (1 << 16), PAD_UP = (1 << 13) | (1 << 17),
    PAD_RIGHT = (1 << 14) | (1 << 18), PAD_DOWN = (1 << 15) | (1 << 19),
};

static int asset_name_valid(const char *name) {
    if (memcmp(name, "asset-", 6) || memcmp(name + 70, ".izdata", sizeof(".izdata"))) return 0;
    for (size_t i = 6; i < 70; ++i)
        if (!(name[i] >= '0' && name[i] <= '9') && !(name[i] >= 'a' && name[i] <= 'f')) return 0;
    return 1;
}

static void enable_controllers(Ps2Sb4App *state, const Sb4Compat *compat) {
    IzServiceFunction ids = compat->resolve_service(5, SB4_HID_SET_SUPPORTED_IDS);
    IzServiceFunction get_styles = compat->resolve_service(5, SB4_HID_GET_SUPPORTED_STYLES);
    IzServiceFunction set_styles = compat->resolve_service(5, SB4_HID_SET_SUPPORTED_STYLES);
    const uint32_t supported_ids[] = {0, 1, 2, 3, 4, 5, 6, 7, 32};
    if (ids) ids((uintptr_t)supported_ids, sizeof(supported_ids) / sizeof(supported_ids[0]),
                 0, 0, 0, 0, 0, 0);
    uint32_t styles = 0;
    if (get_styles && set_styles && !get_styles((uintptr_t)&styles, 0, 0, 0, 0, 0, 0, 0) &&
        (styles & 31) != 31 && !set_styles(styles | 31, 0, 0, 0, 0, 0, 0, 0)) {
        state->original_pad_styles = styles;
        state->restore_pad_styles = 1;
    }
}

static uint32_t draw_menu(const Ps2Sb4App *state, IzNativeApp *app, const char *message) {
    if (!message && !state->bios[0]) message = "COPY A 4 MIB BIOS TO BIOS/    X RESCAN";
    return ps2_sb4_library_draw(&state->games, app, state->selected, state->top, message);
}

static uint32_t scan_media(Ps2Sb4App *state, IzNativeApp *app) {
    Ps2Sb4Library bios = {0};
    uint32_t rc = ps2_sb4_library_refresh(&bios, app, PS2_SB4_LIBRARY_BIOS);
    if (rc) return rc;
    rc = ps2_sb4_library_refresh(&state->games, app, PS2_SB4_LIBRARY_GAMES);
    if (!rc) {
        state->bios[0] = 0;
        if (bios.count) memcpy(state->bios, bios.entries[0].name, sizeof(state->bios));
        state->selected = state->top = 0;
    }
    ps2_sb4_library_close(&bios);
    return rc;
}

static uint32_t launch(Ps2Sb4App *state, IzNativeApp *app, const Sb4Compat *compat, const char *disc) {
    if (!state->bios[0]) return draw_menu(state, app, NULL);
    uint32_t rc = compat->allocate_aligned(&state->audio, 4096, PS2_AUDIO_ARENA_BYTES);
    if (rc || !state->audio) return rc ? rc : PS2_SB4_SESSION_INVALID;
    const Ps2Sb4SessionSetup setup = {
        .engine_asset = state->engine_asset, .bios_filename = state->bios, .disc_filename = disc,
        .video = {.max_width = PS2_SB4_FRAME_WIDTH, .max_height = PS2_SB4_FRAME_HEIGHT},
        .audio_arena = state->audio, .audio_arena_bytes = PS2_AUDIO_ARENA_BYTES,
        .worker = &state->worker, .worker_bytes = sizeof(state->worker)
    };
    rc = ps2_sb4_session_begin(&state->session, app, &setup);
    if (rc && !state->session.app) {
        compat->release(state->audio);
        state->audio = NULL;
        return draw_menu(state, app, "CANNOT START SELECTED MEDIA    X RESCAN");
    }
    if (rc) return rc;
    if (disc != state->current_game)
        memcpy(state->current_game, disc, strlen(disc) + 1);
    const size_t disc_bytes = strlen(disc);
    if (disc_bytes && (strlen(state->settings.prefs.last_game) != disc_bytes ||
                      memcmp(disc, state->settings.prefs.last_game, disc_bytes))) {
        memset(state->settings.prefs.last_game, 0, sizeof(state->settings.prefs.last_game));
        memcpy(state->settings.prefs.last_game, disc, strlen(disc) + 1);
        state->settings.dirty = 1;
        if (ps2_settings_save(&state->settings, app))
            state->settings.warning = PS2_SETTINGS_INVALID;
    }
    return ps2_sb4_loading_draw(app, "Preparing IZPS2...", 0);
}

static uint32_t show_folder(Ps2Sb4App *state, IzNativeApp *app, const char *path, int going_up) {
    uint32_t rc = ps2_sb4_library_open_games(&state->games, app, path);
    if (going_up && rc == PS2_SB4_LIBRARY_NOT_FOUND && *path)
        rc = ps2_sb4_library_open_games(&state->games, app, "");
    if (rc) return draw_menu(state, app, "CANNOT READ SD FOLDER    X RESCAN");
    state->selected = state->top = 0;
    return draw_menu(state, app, NULL);
}

static uint32_t draw_settings(const Ps2Sb4App *state, IzNativeApp *app) {
    uint32_t *pixels = ps2_sb4_menu_pixels(app);
    if (!pixels) return PS2_SB4_LIBRARY_INVALID;
    ps2_settings_menu_draw(pixels, &state->settings_menu, &state->settings, UINT32_MAX);
    ps2_sb4_menu_commit(app);
    return 0;
}

static uint32_t settings_frame(Ps2Sb4App *state, IzNativeApp *app, const Sb4Compat *compat,
                               uint32_t pressed) {
    if (!pressed) return 0;
    const enum Ps2SettingsMenuAction action =
        ps2_settings_menu_step(&state->settings_menu, &state->settings, pressed);
    if (action == PS2_SETTINGS_STAY) return draw_settings(state, app);
    state->settings_menu.error = ps2_settings_save(&state->settings, app);
    if (state->settings_menu.error) return draw_settings(state, app);
    state->settings_open = 0;
    if (action == PS2_SETTINGS_BOOT_BIOS) return launch(state, app, compat, "");
    return draw_menu(state, app, NULL);
}

static uint32_t frame(Ps2Sb4App *state, IzNativeApp *app, const Sb4Compat *compat) {
    const uint64_t buttons = app->buttons(app);
    const uint64_t pressed = buttons & ~state->previous_buttons;
    state->previous_buttons = buttons;
    if (state->settings_open) return settings_frame(state, app, compat, (uint32_t)pressed);
    if ((pressed & PAD_B) && !state->session.worker_ticket) {
        if (!state->session.app && state->games.directory[0]) {
            char parent[sizeof(state->games.directory)];
            memcpy(parent, state->games.directory, sizeof(parent));
            char *slash = strrchr(parent, '/');
            if (slash) *slash = 0;
            else parent[0] = 0;
            return show_folder(state, app, parent, 1);
        }
        app->request_exit(app);
        return 0;
    }
    if (!state->hosted_posix) return 0;
    if (state->session.app) {
        const uint32_t previous = state->session.load_percent;
        const uint32_t result = ps2_sb4_session_step(&state->session);
        if (result == PS2_SB4_SESSION_RESTART_GAME || result == PS2_SB4_SESSION_CHOOSE_GAME ||
            result == PS2_SB4_SESSION_BOOT_BIOS) {
            compat->release(state->audio);
            state->audio = NULL;
            const uint32_t error = ps2_settings_load(&state->settings, app);
            if (error) state->settings.warning = error;
            if (result == PS2_SB4_SESSION_RESTART_GAME)
                return launch(state, app, compat, state->current_game);
            if (result == PS2_SB4_SESSION_BOOT_BIOS)
                return launch(state, app, compat, "");
            return draw_menu(state, app, NULL);
        }
        const uint32_t percent = state->session.load_percent;
        if (result || percent == previous) return result;
        const char *message = percent < 100 ? "Loading IZPS2 engine..." :
            state->worker.media.iso[0] ? "Starting PS2 game..." : "Starting PS2 BIOS...";
        return ps2_sb4_loading_draw(app, message, percent);
    }
    if (pressed & PAD_X) {
        const uint32_t rc = scan_media(state, app);
        return draw_menu(state, app, rc ? "CANNOT READ SD MEDIA    X RESCAN" : NULL);
    }
    if (pressed & PAD_MINUS) {
        state->settings_open = 1;
        state->settings_menu = (Ps2SettingsMenu){0};
        return draw_settings(state, app);
    }
    if ((pressed & PAD_Y) && state->settings.prefs.last_game[0])
        return launch(state, app, compat, state->settings.prefs.last_game);
    if (!state->games.count) return 0;
    if (pressed & PAD_A) {
        const Sb4StorageEntry *entry = &state->games.entries[state->selected];
        char path[PS2_SB4_MEDIA_PATH_MAX];
        if (ps2_sb4_library_game_path(&state->games, entry->name, path))
            return draw_menu(state, app, "CANNOT START SELECTED MEDIA    X RESCAN");
        if (entry->directory) return show_folder(state, app, path, 0);
        return launch(state, app, compat, path);
    }

    uint32_t selected = state->selected;
    const uint32_t last = state->games.count - 1;
    if (pressed & PAD_UP) selected = selected ? selected - 1 : last;
    if (pressed & PAD_DOWN) selected = selected < last ? selected + 1 : 0;
    if (pressed & (PAD_L | PAD_LEFT))
        selected = selected >= PS2_SB4_LIBRARY_ROWS ? selected - PS2_SB4_LIBRARY_ROWS : 0;
    if (pressed & (PAD_R | PAD_RIGHT))
        selected = last - selected >= PS2_SB4_LIBRARY_ROWS ? selected + PS2_SB4_LIBRARY_ROWS : last;
    if (selected == state->selected) return 0;
    state->selected = selected;
    if (selected < state->top) state->top = selected;
    if (selected - state->top >= PS2_SB4_LIBRARY_ROWS)
        state->top = selected - PS2_SB4_LIBRARY_ROWS + 1;
    return draw_menu(state, app, NULL);
}

__attribute__((section(".text.iz_app")))
uint32_t iz_app(IzNativeApp *app, uint32_t event) {
    const Sb4Compat *compat = sb4ModuleCompat(app);
    if (!compat || !app->data || app->data_bytes < sizeof(Ps2Sb4App) ||
        (uintptr_t)app->data % _Alignof(Ps2Sb4App)) return PS2_SB4_SESSION_INVALID;
    Ps2Sb4App *state = app->data;
    if (event == IZ_NATIVE_INIT) {
        memset((unsigned char *)state + sizeof(state->engine_asset), 0,
               sizeof(*state) - sizeof(state->engine_asset));
        if (!asset_name_valid(state->engine_asset) || !app->buttons || !app->request_exit)
            return PS2_SB4_SESSION_INVALID;
        app->flags |= IZ_NATIVE_MANAGED_INPUT;
        state->previous_buttons = app->buttons(app);
        state->hosted_posix = sb4HostPosixVersion(compat) >= SB4_HOST_POSIX_VERSION;
        if (!state->hosted_posix) {
            state->games.compat = compat;
            return draw_menu(state, app, "HOSTEDPOSIX REQUIRED. UPDATE IZLAUNCHER. B BACK");
        }
        enable_controllers(state, compat);
        const uint32_t error = ps2_settings_load(&state->settings, app);
        if (error) state->settings.warning = error;
        const uint32_t rc = scan_media(state, app);
        return rc ? rc : draw_menu(state, app, NULL);
    }
    if (event == IZ_NATIVE_FRAME) return frame(state, app, compat);
    if (event == IZ_NATIVE_STOP) {
        uint32_t rc = ps2_sb4_session_stop(&state->session);
        if (rc) return rc;
        if (state->restore_pad_styles) {
            IzServiceFunction restore = compat->resolve_service(5, SB4_HID_SET_SUPPORTED_STYLES);
            if (!restore) return PS2_SB4_SESSION_RETAINED;
            rc = (uint32_t)restore(state->original_pad_styles, 0, 0, 0, 0, 0, 0, 0);
            if (rc) return rc;
            state->restore_pad_styles = 0;
        }
        ps2_sb4_library_close(&state->games);
        if (state->audio) {
            compat->release(state->audio);
            state->audio = NULL;
        }
        return 0;
    }
    return PS2_SB4_SESSION_INVALID;
}
