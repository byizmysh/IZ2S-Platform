#include "ps2_sb4_layout.h"
#include "ps2_sb4_sd_saves.h"
#include "ps2_sb4_fs.h"
#include "ps2_sd_saves.h"
#ifdef PS2_NATIVE_SD_LIVE_CARD
#include "ps2_sd_card.h"
#endif
#include <sb4/native-app.h>
#include <cstdio>

extern "C" void izps2_boot_stage(IzNativeApp *, const char *, uint32_t);

namespace {
IzNativeApp *owner;
const Ps2SdkFsApi *fs_api;
bool saves_ready;
bool retained;

}

extern "C" int ps2_sb4_sd_saves_attach(IzNativeApp *app) {
    if (owner) return 0;
    const auto *storage = sb4ModuleStorage(app);
    if (!sb4StorageWorkerRead(storage) || !sb4StorageWorkerWrite(storage)) return 0;
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if (!sb4StorageFileChanges(storage)) return 0;
#endif
    fs_api = ps2_sb4_fs_bind(app);
    if (!fs_api) return 0;
    owner = app;
    return 1;
}

extern "C" const Ps2SdkFsApi *ps2_sb4_sd_saves_files() {
    return retained ? nullptr : fs_api;
}

extern "C" int ps2_sd_saves_native_initialize() {
    if (retained || !fs_api) return 0;
    if (saves_ready) return 1;
    uint32_t result = ps2_sb4_fs_ensure_directory("saves");
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if (!result) result = ps2_sb4_fs_ensure_directory("saves/cards");
#endif
    if (result) {
        std::fprintf(stderr, "IZPS2 libsb4 save directory setup failed: 0x%08x\n", result);
        return 0;
    }
    if (!ps2::ConfigureSdSaves(*fs_api, PS2_SB4_SAVES_ROOT)) return 0;
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if (!ps2::ConfigureLiveSdCard(*fs_api, ps2_sb4_fs_rename_save, PS2_SB4_SAVES_ROOT "/cards")) {
        retained = !ps2::ShutdownSdSaves();
        return 0;
    }
#endif
    saves_ready = true;
    return 1;
}

extern "C" uint32_t ps2_sb4_sd_saves_detach(IzNativeApp *app) {
    if (!owner) return 0;
    if (owner != app) return PS2_FS_INVALID;
    saves_ready = false;
#ifdef PS2_NATIVE_SD_LIVE_CARD
    const bool cards_closed = ps2::ShutdownLiveSdCard();
#else
    constexpr bool cards_closed = true;
#endif
    const bool saves_closed = ps2::ShutdownSdSaves();
    char outcome[100];
    const int size = std::snprintf(outcome, sizeof(outcome), "36 SD cards_closed=%u saves_closed=%u\n",
        unsigned(cards_closed), unsigned(saves_closed));
    if (size > 0 && static_cast<size_t>(size) < sizeof(outcome))
        izps2_boot_stage(nullptr, outcome, static_cast<uint32_t>(size));
    const uint32_t result = cards_closed && saves_closed
        ? (fs_api ? ps2_sb4_fs_unbind() : 0) : (uint32_t)PS2_FS_INVALID;
    if (result) {
        retained = true;
        return result;
    }
    owner = nullptr;
    fs_api = nullptr;
    retained = false;
    return 0;
}

extern "C" void ps2_sd_saves_native_shutdown() {
    if (ps2_sb4_sd_saves_detach(owner))
        std::fprintf(stderr, "IZPS2 libsb4 save files remain owned after shutdown failure\n");
}
