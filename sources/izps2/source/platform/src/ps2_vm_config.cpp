#include "ps2_vm_runner.h"
#include "Host.h"
#include "SIO/Pad/Pad.h"
#include "VMManager.h"
#include "common/Console.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"

MemorySettingsInterface &ps2_vm_configure(const char *data_root, const char *resources, const char *bios_directory,
                                        const char *card_directory) {
    static MemorySettingsInterface settings, secrets;
    static const bool bound = [] {
        Host::Internal::SetBaseSettingsLayer(&settings);
        Host::Internal::SetSecretsSettingsLayer(&secrets);
        return true;
    }();
    (void)bound;
    EmuFolders::AppRoot = data_root;
    EmuFolders::DataRoot = data_root;
    EmuFolders::Settings = Path::Combine(data_root, "inis");
    EmuFolders::Resources = resources;

    settings.Clear();
    secrets.Clear();
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
    VMManager::Internal::LoadStartupSettings();
    return settings;
}

void ps2_vm_close_log() {
    Log::SetFileOutputLevel(LOGLEVEL_NONE, {});
}
