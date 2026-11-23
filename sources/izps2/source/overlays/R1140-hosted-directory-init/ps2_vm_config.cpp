struct IzNativeApp;
#include "ps2_vm_runner.h"
#include "Host.h"
#include "SIO/Pad/Pad.h"
#include "VMManager.h"
#include "common/Console.h"
#include "common/MemorySettingsInterface.h"
#include <cstdio>
#include "common/Path.h"

uint32_t ps2_sb4_vm_fs_ensure_vm_directory(const char *path);
extern "C" void izps2_boot_stage(IzNativeApp *, const char *, uint32_t);
#define BOOT(text) izps2_boot_stage(nullptr, text "\n", sizeof(text))

MemorySettingsInterface &ps2_vm_configure(const char *data_root, const char *resources, const char *bios_directory,
                                        const char *card_directory) {
    BOOT("15 initialize settings");
    static MemorySettingsInterface settings, secrets;
    static const bool bound = [] {
        Host::Internal::SetBaseSettingsLayer(&settings);
        Host::Internal::SetSecretsSettingsLayer(&secrets);
        return true;
    }();
    (void)bound;
    BOOT("16 initialize folders");
    EmuFolders::AppRoot = data_root;
    EmuFolders::DataRoot = data_root;
    EmuFolders::Settings = Path::Combine(data_root, "inis");
    EmuFolders::Resources = resources;

    settings.Clear();
    secrets.Clear();
    BOOT("17 default settings");
    VMManager::SetDefaultSettings(settings, true, true, true, false, true);
    // libsb4 supplies controller state directly to the emulated pad.
    for (u32 port = 0; port < Pad::NUM_CONTROLLER_PORTS; ++port)
        Pad::ClearPortBindings(settings, port);
    settings.SetStringValue("Folders", "Bios", bios_directory);
    if (card_directory) settings.SetStringValue("Folders", "MemoryCards", card_directory);
    settings.SetIntValue("EmuCore/GS", "Renderer", int(GSRendererType::SW));
    settings.SetIntValue("EmuCore/GS", "extrathreads", 0);
    settings.SetBoolValue("EmuCore/GS", "GSBackThreadMode", false);
    settings.SetBoolValue("EmuCore", "EnableThreadPinning", false);
    settings.SetBoolValue("InputSources", "SDL", false);
    settings.SetStringValue("SPU2/Output", "Backend", "Ounce");
    settings.SetBoolValue("EmuCore/Speedhacks", "vuThread", false);
    settings.SetBoolValue("EmuCore/Speedhacks", "iopThread", false);
    // VMManager checks the actual code allocation before activating the CPU backends.
    for (const char *key : {"EnableFastmem", "EnableVUAot", "EnableVUProgramCache"})
        settings.SetBoolValue("EmuCore/CPU/Recompiler", key, false);
    settings.SetBoolValue("Achievements", "Enabled", false);
    settings.SetBoolValue("EmuCore", "EnablePINE", false);
    settings.SetBoolValue("EmuCore", "EnableDiscordPresence", false);
    settings.SetBoolValue("DEV9/Eth", "EthEnable", false);
    settings.SetBoolValue("Logging", "EnableSystemConsole", false);
    settings.SetBoolValue("Logging", "EnableFileLogging", true);
    // CPUThreadInitialize reads these before LoadSettings.
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
    BOOT("18 load startup settings");
    VMManager::Internal::LoadStartupSettings();
    BOOT("19 startup settings ready");
    return settings;
}

void ps2_vm_close_log() {
    Log::SetFileOutputLevel(LOGLEVEL_NONE, {});
}

extern "C" void real_folders_load(SettingsInterface&)
    asm("__real__ZN10EmuFolders10LoadConfigER17SettingsInterface");
extern "C" void wrap_folders_load(SettingsInterface& settings)
    asm("__wrap__ZN10EmuFolders10LoadConfigER17SettingsInterface");
void wrap_folders_load(SettingsInterface& settings) {
    BOOT("18a load folder paths");
    real_folders_load(settings);
    BOOT("18b folder paths ready");
}

extern "C" bool wrap_make_folders() asm("__wrap__ZN10EmuFolders18EnsureFoldersExistEv");
bool wrap_make_folders() {
    BOOT("18c verify BIOS and create folders using module storage");
    const std::string *const directories[] = {
        &EmuFolders::Bios, &EmuFolders::Settings, &EmuFolders::Snapshots,
        &EmuFolders::Savestates, &EmuFolders::MemoryCards, &EmuFolders::Logs,
        &EmuFolders::Cheats, &EmuFolders::Patches, &EmuFolders::Covers,
        &EmuFolders::GameSettings, &EmuFolders::UserResources, &EmuFolders::Cache,
        &EmuFolders::Textures, &EmuFolders::InputProfiles,
        &EmuFolders::DebuggerLayouts, &EmuFolders::DebuggerSettings,
    };
    bool ok = true;
    for (const auto *directory : directories) {
        if (const uint32_t result = ps2_sb4_vm_fs_ensure_vm_directory(directory->c_str())) {
            char message[192];
            const int len = std::snprintf(message, sizeof(message), "mkdir 0x%08x %.140s\n", result,
                                          directory->c_str());
            if (len > 0 && len < static_cast<int>(sizeof(message)))
                izps2_boot_stage(nullptr, message, static_cast<uint32_t>(len));
            ok = false;
        }
    }
    BOOT("18d module directory setup returned");
    return ok;
}

extern "C" bool real_set_file_log(LOGLEVEL, std::string)
    asm("__real__ZN3Log18SetFileOutputLevelE8LOGLEVELNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE");
extern "C" bool wrap_set_file_log(LOGLEVEL level, std::string path)
    asm("__wrap__ZN3Log18SetFileOutputLevelE8LOGLEVELNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE");
bool wrap_set_file_log(LOGLEVEL level, std::string path) {
    BOOT("18e configure file log");
    const bool ok = real_set_file_log(level, std::move(path));
    BOOT("18f configure file log returned");
    return ok;
}
