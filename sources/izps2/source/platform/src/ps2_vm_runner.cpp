#ifdef ENABLE_VULKAN
#include "GS/Renderers/Vulkan/VKLoader.h"
#include "GS/Renderers/Common/GSHeadlessPresent.h"
#endif
#include "ps2_vm_runner.h"
#include "ps2_sb4_session.h"
#ifdef PS2_EE_DISPATCH_AB
#include "ps2_sb4_layout.h"
#include "arm64/iR5900-arm64.h"
#include "common/FileSystem.h"
#endif
#include "native_pad_frontend.h"
#include "native_audio_stream.h"
#include "ps2_sb4_live_audio.h"
#include "CDVD/CDVDcommon.h"
#include "Host.h"
#include "MTVU.h"
#include "R5900.h"
#include "SIO/Pad/PadDualshock2.h"
#include "VMManager.h"
#ifdef IZPS2_BUILD_REVISION
#include "common/Console.h"
#endif
#include "common/Error.h"
#include "common/MemorySettingsInterface.h"
#include "common/Threading.h"
#include "common/Timer.h"
#include "video.h"
#ifdef IZPS2_WRAPPER_AUDIT
#include "EECycleRateSampler.h"
#include "ps2_sb4_fs.h"
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB
#include "Memory.h"
#include "iop-off-timing-private.h"
#include <zlib.h>

void recSetSoftmemOptimizationsForTest(bool enabled);
#endif
#if defined(IZPS2_TEST_GTA_FRAME821) || defined(IZPS2_TEST_GTA_CHECKPOINT)
#include "SaveState.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#endif
#ifdef __SWITCH2__
#include "common/Switch2/CodeCache.h"
#include "native-system.h"
#include <sb4/jit-cache.h>
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
#include "ps2_sb4_layout.h"
#include "common/FileSystem.h"
#include "common/ScopedGuard.h"

namespace NativePoll {
void ConfigureTrial(bool enabled);
void LogTrial();
}
#endif
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
#include "ps2_sb4_layout.h"
#include "common/FileSystem.h"
void izConfigureEeStartupTrace(bool enabled);
#ifdef IZPS2_SWITCH2_VU_STARTUP_TRACE
void izConfigureVuStartupTrace(bool enabled);
#endif
#endif
#include <atomic>
#include <cstdio>

#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
#include "common/Console.h"
#include "ps2_frame_windows.inc"
#endif

#ifdef IZPS2_SWITCH2_EE_WARM
#include "Memory.h"
#include "GS/GSCacheFile.h"
#include "common/Console.h"
#include "native_ee_warm_retain.hpp"
u32 recEeWarmBatch(std::span<const ps2::ee_warm::Entry>);
void recEeStartWarmProfileWithLimit(u32, u32, u32);
size_t recEeFinishWarmProfile(const std::string&, std::span<u8>);
#include "ps2_sb4_ee_warm.inc"
#endif

namespace {
std::atomic_flag vm_busy = ATOMIC_FLAG_INIT;
#ifdef IZPS2_TEST_SOFTMEM_AB
constexpr uint64_t softmem_warm_frames = 240;
constexpr uint64_t softmem_sample_frames = 480;

struct SoftmemSample {
    uint64_t frames = 0, gs_frames = 0, cpu_ticks = 0;
    Common::Timer::Value wall = 0;
    Ps2IopOffSampleTiming iop{};
    HostSys::JitCacheTiming jit{};
};

struct SoftmemTrial {
    uint64_t initial_frames = 0;
    bool optimized = false;
    bool interrupted = false;
    SoftmemSample first, last;
};

SoftmemSample sample_softmem_trial(uint64_t frames) {
    SoftmemSample sample;
    sample.wall = Common::Timer::GetCurrentValue();
    sample.frames = frames;
    sample.gs_frames = GSHeadless::PresentCount();
    sample.cpu_ticks = Threading::GetThreadCpuTime();
    ps2_iop_off_sample_timing_snapshot(&sample.iop);
    sample.jit = HostSys::GetJitCacheTiming();
    return sample;
}

void observe_softmem_trial(SoftmemTrial& trial, VMState state) {
    if (state != VMState::Running) {
        trial.interrupted = true;
        return;
    }
    if (trial.last.frames) return;
    EECycleRateTally tally;
    if (!EECycleRateSampler::GetTally(&tally)) return;
    if (!trial.first.frames && tally.frames - trial.initial_frames >= softmem_warm_frames)
        trial.first = sample_softmem_trial(tally.frames);
    else if (trial.first.frames && tally.frames - trial.first.frames >= softmem_sample_frames)
        trial.last = sample_softmem_trial(tally.frames);
}

void report_softmem_trial(const SoftmemTrial& trial) {
    const auto& first = trial.first;
    const auto& last = trial.last;
    const u32 ram_crc = crc32(0, eeMem->Main, Ps2MemSize::ExposedRam);
    std::fprintf(stderr, "IZPS2_SOFTMEM_TRIAL optimized=%d frames=%llu gs_frames=%llu wall_ms=%.3f "
        "ee_cpu_ticks=%llu cpu_hz=%llu iop_calls=%llu iop_samples=%llu iop_sample_ticks=%llu "
        "iop_hz=%llu jit_calls=%llu jit_bytes=%llu jit_ticks=%llu jit_hz=%llu "
        "ee_pc=%08x ee_cycle=%llu ram_crc=%08x\n", trial.optimized,
        static_cast<unsigned long long>(last.frames - first.frames),
        static_cast<unsigned long long>(last.gs_frames - first.gs_frames),
        Common::Timer::ConvertValueToMilliseconds(last.wall - first.wall),
        static_cast<unsigned long long>(last.cpu_ticks >= first.cpu_ticks ? last.cpu_ticks - first.cpu_ticks : 0),
        static_cast<unsigned long long>(Threading::GetThreadTicksPerSecond()),
        static_cast<unsigned long long>(last.iop.calls - first.iop.calls),
        static_cast<unsigned long long>(last.iop.valid - first.iop.valid),
        static_cast<unsigned long long>(last.iop.total_ticks - first.iop.total_ticks),
        static_cast<unsigned long long>(last.iop.tick_hz),
        static_cast<unsigned long long>(last.jit.calls - first.jit.calls),
        static_cast<unsigned long long>(last.jit.bytes - first.jit.bytes),
        static_cast<unsigned long long>(last.jit.ticks - first.jit.ticks),
        static_cast<unsigned long long>(iz_tick_frequency()),
        cpuRegs.pc, static_cast<unsigned long long>(cpuRegs.cycle), ram_crc);
}
#endif

struct ModuleControl {
    const Ps2VmControl *callbacks;
    bool stop_pending;
    bool cpu_active;
#ifdef IZPS2_TEST_GTA_CHECKPOINT
    bool gta_checkpoint = false;
    bool checkpoint_pending = false;
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB
    SoftmemTrial* softmem_trial = nullptr;
#endif
};
ModuleControl module_control;

#if defined(IZPS2_TEST_GTA_FRAME821) || defined(IZPS2_TEST_GTA_CHECKPOINT)
thread_local bool saving_test_checkpoint = false;

bool save_test_checkpoint(const std::string& checkpoint, bool backup) {
    std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT saving=%s\n", checkpoint.c_str());
    bool saved = true;
    saving_test_checkpoint = true;
    VMManager::SaveState(checkpoint.c_str(), false, backup, [&](const std::string &message) {
        saved = false;
        std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT save=failed error=%s\n", message.c_str());
    });
    saving_test_checkpoint = false;
    if (saved && FileSystem::GetPathFileSize(checkpoint.c_str()) <= 0) {
        saved = false;
        std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT save=failed error=checkpoint-missing\n");
    }
    return saved;
}
#endif

struct VmOwner {
    ~VmOwner() {
        module_control = {};
        vm_busy.clear(std::memory_order_release);
    }
};

bool stop_requested() {
    const auto *callbacks = module_control.callbacks;
    return callbacks && __atomic_load_n(callbacks->stop_requested, __ATOMIC_ACQUIRE);
}

struct CpuOwner {
    ~CpuOwner() {
        VMManager::Internal::CPUThreadShutdown();
        vu1Thread.Close();
#ifdef __SWITCH2__
        HostSys::SetJitCacheApi(nullptr);
#endif
    }
};

struct ModulePoll {
    void (*extra)(void *);
    void *context;
    Sb4InputFocusReader input_focused;
    bool paused_for_focus = false;
    bool r3_down = false;
    Common::Timer::Value first_r3 = 0;
    uint64_t pad_switches = 0;
    uint64_t pad_disconnects = 0;
#ifdef IZPS2_WRAPPER_AUDIT
    uint64_t pad_polls = 0;
    Common::Timer::Value pad_ticks = 0, focus_ticks = 0;
#endif
#ifdef IZPS2_TEST_GTA_CHECKPOINT
    bool checkpoint_buttons_down = false;
#endif
};

void poll_module(void *data) {
    auto &poll = *static_cast<ModulePoll *>(data);
#ifdef IZPS2_WRAPPER_AUDIT
    const auto pad_start = Common::Timer::GetCurrentValue();
#endif
    const bool pad_ok = ps2_native_pad_poll() == 0;
#ifdef IZPS2_WRAPPER_AUDIT
    ++poll.pad_polls;
    poll.pad_ticks += Common::Timer::GetCurrentValue() - pad_start;
#endif
    if (poll.extra) poll.extra(poll.context);
    const auto state = VMManager::GetState();
#ifdef IZPS2_WRAPPER_AUDIT
    const auto focus_start = Common::Timer::GetCurrentValue();
#endif
    const bool focused = poll.input_focused() != 0;
#ifdef IZPS2_WRAPPER_AUDIT
    poll.focus_ticks += Common::Timer::GetCurrentValue() - focus_start;
#endif
    // The ABI 7 frontend owns game input, including the normal double-R3 exit.
    Ps2NativePadStatistics pad{};
    if (focused && pad_ok && (state == VMState::Running || state == VMState::Paused) &&
        ps2_native_pad_statistics(&pad) == 0 && pad.frame.connected) {
        const bool same_pad = pad.switches == poll.pad_switches && pad.disconnects == poll.pad_disconnects;
        const bool down = (pad.frame.buttons & (1u << PadDualshock2::PAD_R3)) != 0;
#ifdef IZPS2_TEST_GTA_CHECKPOINT
        const bool checkpoint_buttons = module_control.gta_checkpoint && down &&
            (pad.frame.buttons & (1u << PadDualshock2::PAD_L3)) != 0;
        if (checkpoint_buttons && same_pad && !poll.checkpoint_buttons_down && state == VMState::Running &&
            VMManager::Internal::HasBootedELF()) {
            module_control.checkpoint_pending = true;
            std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT requested=input\n");
        }
#else
        constexpr bool checkpoint_buttons = false;
#endif
        if (!same_pad || checkpoint_buttons)
            poll.first_r3 = 0;
        else if (down && !poll.r3_down) {
            const auto now = Common::Timer::GetCurrentValue();
            if (poll.first_r3 && now - poll.first_r3 <= Common::Timer::ConvertMillisecondsToValue(350)) {
                std::fprintf(stderr, "IZPS2_VM_STOP_REQUEST input=double-r3\n");
                module_control.stop_pending = true;
                return;
            }
            poll.first_r3 = now;
        }
        poll.r3_down = down;
        poll.pad_switches = pad.switches;
        poll.pad_disconnects = pad.disconnects;
#ifdef IZPS2_TEST_GTA_CHECKPOINT
        poll.checkpoint_buttons_down = checkpoint_buttons;
#endif
    } else {
        poll.first_r3 = 0;
        poll.r3_down = false;
#ifdef IZPS2_TEST_GTA_CHECKPOINT
        poll.checkpoint_buttons_down = false;
#endif
    }
    if (!focused && state == VMState::Running) {
        poll.paused_for_focus = true;
        VMManager::SetPaused(true);
    } else if (poll.paused_for_focus && focused) {
        poll.paused_for_focus = false;
        if (state == VMState::Paused) VMManager::SetPaused(false);
    }
}
}

#if defined(IZPS2_TEST_GTA_FRAME821) || defined(IZPS2_TEST_GTA_CHECKPOINT)
extern "C" std::unique_ptr<SaveStateScreenshotData> __real__Z24SaveState_SaveScreenshotv();
extern "C" std::unique_ptr<SaveStateScreenshotData> __wrap__Z24SaveState_SaveScreenshotv() {
    // This checkpoint needs guest state. The thumbnail would add a separate GS download.
    if (saving_test_checkpoint) return {};
    return __real__Z24SaveState_SaveScreenshotv();
}
#endif

bool ps2_vm_pump_module(bool host_stop) {
    const auto *callbacks = module_control.callbacks;
    if (!callbacks) return false;
    module_control.stop_pending = module_control.stop_pending || host_stop || stop_requested();
    if (!module_control.stop_pending && callbacks->poll) {
        callbacks->poll(callbacks->context);
        module_control.stop_pending = module_control.stop_pending || stop_requested();
    }
    if (module_control.cpu_active) {
        const auto state = VMManager::GetState();
        if (module_control.stop_pending && (state == VMState::Running || state == VMState::Paused))
            VMManager::SetState(VMState::Stopping);
        // The ARM64 provider exits at the next safe event when requested.
        // A pause on its own leaves its Execute call active.
        bool return_to_owner = state == VMState::Paused;
#ifdef IZPS2_TEST_GTA_CHECKPOINT
        return_to_owner |= module_control.checkpoint_pending;
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB
        if (auto* trial = module_control.softmem_trial) {
            observe_softmem_trial(*trial, state);
            return_to_owner |= trial->interrupted || trial->last.frames != 0;
        }
#endif
        if (return_to_owner) Cpu->ExitExecution();
    }
    return true;
}

Ps2VmRun ps2_run_vm(const VMBootParameters &boot, void (*sleep_ns)(uint64_t), const Ps2VmControl *control) {
    if (!sleep_ns || (control && !control->stop_requested))
        return {Ps2VmResult::Invalid, false, "Missing PS2 VM callbacks."};
    if (vm_busy.test_and_set(std::memory_order_acquire))
        return {Ps2VmResult::Busy, false, "PS2 CPU already in use."};
    const VmOwner vm;
    module_control = {control, false, false};
#ifdef IZPS2_TEST_SOFTMEM_AB
    recSetSoftmemOptimizationsForTest(false);
    SoftmemTrial softmem_trial;
#endif
    if (stop_requested()) return {Ps2VmResult::Stopped, false, {}};
    const char *hardware_error = nullptr;
    if (!VMManager::PerformEarlyHardwareChecks(&hardware_error))
        return {Ps2VmResult::HardwareFailed, false,
                hardware_error ? hardware_error : "CPU capability check failed."};

#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
    const bool poll_latch_requested = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-crossblock-poll.flag");
    const bool poll_latch_trial = poll_latch_requested || (control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-crossblock-poll-control.flag"));
    NativePoll::ConfigureTrial(poll_latch_requested);
    struct PollTrialSetting {
        const char *section;
        const char *key;
        bool saved;
        bool present;
    };
    PollTrialSetting poll_settings[] = {
#ifndef IZPS2_SWITCH2_NATIVE_POLL_KEEP_TFX
        {"EmuCore/GS", "PrecompilePipelines", false, false},
#endif
        {"EmuCore/CPU/Recompiler", "EnableVUAot", false, false},
        {"EmuCore/CPU/Recompiler", "EnableVUProgramCache", false, false},
    };
    SettingsInterface *poll_base = nullptr;
    if (poll_latch_trial) {
        const auto lock = Host::GetSettingsLock();
        poll_base = Host::Internal::GetBaseSettingsLayer();
        if (poll_base) {
            for (auto &setting : poll_settings) {
                setting.present = poll_base->GetBoolValue(setting.section, setting.key, &setting.saved);
                poll_base->SetBoolValue(setting.section, setting.key, false);
            }
        }
    }
    const ScopedGuard restore_poll_trial([&] {
        if (poll_base) {
            const auto lock = Host::GetSettingsLock();
            for (const auto &setting : poll_settings) {
                if (setting.present)
                    poll_base->SetBoolValue(setting.section, setting.key, setting.saved);
                else
                    poll_base->DeleteValue(setting.section, setting.key);
            }
        }
    });
#endif
#ifdef PS2_EE_DISPATCH_AB
    const bool direct_ram_dispatch = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-direct-ram.flag");
    recSelectDirectRamDispatch(direct_ram_dispatch);
#endif
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
    const bool trace_ee_startup = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-startup-trace.flag");
    izConfigureEeStartupTrace(trace_ee_startup);
#ifdef IZPS2_SWITCH2_VU_STARTUP_TRACE
    izConfigureVuStartupTrace(trace_ee_startup);
#endif
#endif
    const CpuOwner cpu;
#ifdef __SWITCH2__
    HostSys::SetJitCacheApi(control ? control->jit_cache : nullptr);
#endif
    if (control) std::fprintf(stderr, "IZPS2 VM: initializing CPU and input\n");
    if (!VMManager::Internal::CPUThreadInitialize())
        return {Ps2VmResult::CpuFailed, true, "CPU initialization failed. See native.log and emuLog.txt."};
    if (stop_requested()) return {Ps2VmResult::Stopped, true, {}};
    if (control) std::fprintf(stderr, "IZPS2 VM: CPU ready, opening selected media\n");
    Error error;
    if (VMManager::Initialize(boot, &error) != VMBootResult::StartupSuccess)
        return {Ps2VmResult::BootFailed, true, error.GetDescription()};

    if (control) std::fprintf(stderr, "IZPS2 VM: media open, starting emulation\n");
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    if (control) Console.WriteLn("IZPS2_BUILD " IZ_JIT_BUILD_ID);
#ifdef IZPS2_SWITCH2_VU_STARTUP_TRACE
    if (control)
        Console.WriteLn("IZPS2_JIT_DIAGNOSTIC scalar_control_requested=%d",
            FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/jit-scalar-publication.flag") ? 1 : 0);
#endif
#endif
#ifdef PS2_EE_DISPATCH_AB
    if (control)
        Console.WriteLn("IZPS2_EE_DISPATCH direct_ram=%d", direct_ram_dispatch ? 1 : 0);
#endif
#ifdef IZPS2_SWITCH2_EE_WARM
    Sb4EeWarmProfile ee_warm;
#endif
#if defined(IZPS2_TEST_GTA_FRAME821) || defined(IZPS2_TEST_GTA_CHECKPOINT)
    const bool gta = control && VMManager::GetDiscCRC() == 0xb440a8fe &&
        VMManager::GetDiscSerial() == "SLES-52541";
#ifdef IZPS2_TEST_GTA_FRAME821
    constexpr unsigned checkpoint_frame = 821;
    const std::string checkpoint = Path::Combine(EmuFolders::Savestates, "izps2-r799-gta-sa-frame821.p2s");
#else
    module_control.gta_checkpoint = gta;
    const std::string checkpoint = Path::Combine(EmuFolders::Savestates, "izps2-r799-gta-sa-entry.p2s");
#endif
    const bool restore_checkpoint = gta && FileSystem::FileExists(checkpoint.c_str());
    if (restore_checkpoint) {
#ifdef IZPS2_SWITCH2_SESSION_TIMING
        const auto before_load = sample_session_timing();
#endif
        const bool loaded = VMManager::LoadState(checkpoint.c_str(), &error);
#ifdef IZPS2_SWITCH2_SESSION_TIMING
        report_session_timing(loaded ? "checkpoint" : "checkpoint_failed", 0, before_load, sample_session_timing());
#endif
        if (loaded) {
            std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT loaded=%s\n", checkpoint.c_str());
            Host::AddOSDMessage("GTA SA checkpoint loaded.");
#ifdef IZPS2_SWITCH2_EE_WARM
#ifdef IZPS2_SWITCH2_SESSION_TIMING
            const auto before_warm = sample_session_timing();
#endif
            if (control) ee_warm = start_sb4_ee_warm(*control);
#ifdef IZPS2_SWITCH2_SESSION_TIMING
            report_session_timing("ee_warm", 0, before_warm, sample_session_timing());
#endif
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB
            if (SysMemory::HasCodeMemory() && Cpu == &recCpu) {
                module_control.softmem_trial = &softmem_trial;
                std::fprintf(stderr, "IZPS2_SOFTMEM_TRIAL_BEGIN warm_frames=%llu sample_frames=%llu\n",
                    static_cast<unsigned long long>(softmem_warm_frames),
                    static_cast<unsigned long long>(softmem_sample_frames));
            } else {
                std::fprintf(stderr, "IZPS2_SOFTMEM_TRIAL_SKIP reason=ee-jit-inactive\n");
            }
#endif
        } else {
            const std::string message = error.GetDescription();
            std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT load=failed error=%s\n", message.c_str());
            Host::AddOSDMessage("GTA SA checkpoint load failed.");
        }
    }
#endif

#ifdef IZPS2_TEST_GTA_FRAME821
    const bool capture_checkpoint = gta && !restore_checkpoint;
    VMManager::FrameAdvance(capture_checkpoint ? checkpoint_frame : 0);
    if (capture_checkpoint)
        std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT armed_frame=%u\n", checkpoint_frame);
#else
    VMManager::SetState(VMState::Running);
#endif
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
    if (control) s_frame_windows.Start();
#endif
#ifdef IZPS2_BUILD_REVISION
    Host::AddOSDMessage("IZPS2 " IZPS2_BUILD_REVISION, 4.0f);
#endif
    bool first_dispatch = control != nullptr;
    if (first_dispatch) std::fprintf(stderr, "IZPS2 VM: VM running, polling input\n");
    const auto start_wall = Common::Timer::GetCurrentValue();
    const uint64_t start_cpu = Threading::GetThreadCpuTime();
#ifdef IZPS2_WRAPPER_AUDIT
    const Ps2Sb4ReadTiming start_reads = ps2_sb4_fs_read_timing();
    EECycleRateSampler::RequestTally(control != nullptr);
#endif
    while (VMManager::GetState() != VMState::Stopping) {
        Host::PumpMessagesOnCPUThread();
        if (module_control.stop_pending) break;
#ifdef IZPS2_TEST_SOFTMEM_AB
        if (module_control.softmem_trial && softmem_trial.interrupted) {
            std::fprintf(stderr, "IZPS2_SOFTMEM_TRIAL_ABORT reason=paused\n");
            break;
        }
        if (module_control.softmem_trial && softmem_trial.last.frames) {
            report_softmem_trial(softmem_trial);
            if (softmem_trial.optimized) break;

            // The VM owner is outside Cpu->Execute. Restoring the same state
            // clears the generated code before the second measurement.
            module_control.softmem_trial = nullptr;
            recSetSoftmemOptimizationsForTest(true);
            if (!VMManager::LoadState(checkpoint.c_str(), &error)) {
                std::fprintf(stderr, "IZPS2_SOFTMEM_TRIAL_ABORT error=%s\n", error.GetDescription().c_str());
                break;
            }
            VMManager::SetState(VMState::Running);
            EECycleRateTally tally;
            EECycleRateSampler::GetTally(&tally);
            softmem_trial = {.initial_frames = tally.frames, .optimized = true};
            module_control.softmem_trial = &softmem_trial;
        }
#endif
#ifdef IZPS2_TEST_GTA_FRAME821
        if (capture_checkpoint && VMManager::GetState() == VMState::Paused && !VMManager::IsFrameAdvancing()) {
            if (!VMManager::Internal::HasBootedELF()) {
                std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT capture=skipped reason=elf-not-started\n");
                break;
            }
            if (save_test_checkpoint(checkpoint, false))
                std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT saved_frame=%u path=%s\n", checkpoint_frame, checkpoint.c_str());
            break;
        }
#endif
#ifdef IZPS2_TEST_GTA_CHECKPOINT
        if (module_control.checkpoint_pending) {
            module_control.checkpoint_pending = false;
            if (save_test_checkpoint(checkpoint, true)) {
                std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT saved=%s\n", checkpoint.c_str());
                Host::AddOSDMessage("GTA SA checkpoint saved.");
            } else {
                Host::AddOSDMessage("GTA SA checkpoint save failed.");
            }
        }
#endif
        if (VMManager::GetState() == VMState::Running) {
            if (first_dispatch) std::fprintf(stderr, "IZPS2 VM: dispatching first CPU slice\n");
            module_control.cpu_active = true;
            VMManager::Execute();
            module_control.cpu_active = false;
            if (first_dispatch) {
                std::fprintf(stderr, "IZPS2 VM: first CPU slice returned\n");
                first_dispatch = false;
            }
        } else if (VMManager::GetState() == VMState::Paused) {
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
            s_frame_windows.Pause();
#endif
            VMManager::IdlePollUpdate();
            sleep_ns(1000000);
        } else {
            break;
        }
    }
    if (control) {
        const uint64_t end_cpu = Threading::GetThreadCpuTime();
        const uint64_t cpu_ticks = start_cpu && end_cpu >= start_cpu ? end_cpu - start_cpu : 0;
        std::fprintf(stderr, "IZPS2_VM_TIMING ee_cpu_ticks=%llu cpu_tick_hz=%llu wall_ms=%.3f\n",
            static_cast<unsigned long long>(cpu_ticks),
            static_cast<unsigned long long>(Threading::GetThreadTicksPerSecond()),
            Common::Timer::ConvertValueToMilliseconds(Common::Timer::GetCurrentValue() - start_wall));
    }
#ifdef IZPS2_WRAPPER_AUDIT
    EECycleRateTally tally;
    if (EECycleRateSampler::GetTally(&tally)) {
        const auto reads = ps2_sb4_fs_read_timing();
        std::fprintf(stderr, "IZPS2_WRAPPER_TIMING frames=%llu overran=%llu gs_wait_ms=%.3f vu1_wait_ms=%.3f "
            "sd_reads=%llu sd_read_bytes=%llu sd_read_ms=%.3f sd_read_errors=%llu\n",
            static_cast<unsigned long long>(tally.frames), static_cast<unsigned long long>(tally.overran_frames),
            Common::Timer::ConvertValueToMilliseconds(tally.gs_wait_ticks),
            Common::Timer::ConvertValueToMilliseconds(tally.vu1_wait_ticks),
            static_cast<unsigned long long>(reads.calls - start_reads.calls),
            static_cast<unsigned long long>(reads.bytes - start_reads.bytes),
            Common::Timer::ConvertValueToMilliseconds(reads.ticks - start_reads.ticks),
            static_cast<unsigned long long>(reads.errors - start_reads.errors));
    }
    EECycleRateSampler::RequestTally(false);
#endif
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
    if (control) report_frame_windows();
#endif
#ifdef IZPS2_SWITCH2_EE_WARM
    finish_sb4_ee_warm(ee_warm);
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
    if (poll_latch_trial) NativePoll::LogTrial();
#endif
    VMManager::Shutdown(false);
#ifdef IZPS2_TEST_SOFTMEM_AB
    recSetSoftmemOptimizationsForTest(false);
#endif
    return {Ps2VmResult::Stopped, true, {}};
}

extern "C" uint32_t ps2_sb4_run_vm(Ps2Sb4Worker *worker, MemorySettingsInterface *settings,
                                    void (*poll)(void *), void *context) {
    const Sb4Compat *compat = worker ? sb4ModuleCompat(worker->app) : nullptr;
    if (!compat || !settings || !worker->media.bios_filename[0])
        return PS2_SB4_SESSION_INVALID;
    if (__atomic_load_n(&worker->stop_requested, __ATOMIC_ACQUIRE))
        return static_cast<uint32_t>(Ps2VmResult::Stopped);
    const auto input_focused = reinterpret_cast<Sb4InputFocusReader>(compat->resolve_c("sb4InputFocused"));
    if (!input_focused) {
        std::fprintf(stderr, "IZPS2 VM: Launcher does not expose libsb4 input focus.\n");
        return static_cast<uint32_t>(Ps2VmResult::InputFailed);
    }
    if (!worker->video || !iz_video_initialize(worker->video, input_focused)) {
        std::fprintf(stderr, "IZPS2 VM: Cannot attach libsb4 video output.\n");
        return static_cast<uint32_t>(Ps2VmResult::VideoFailed);
    }
#ifdef IZPS2_BUILD_REVISION
    Console.WriteLn("IZPS2 " IZPS2_BUILD_REVISION " video swapchain images=%u", worker->video->image_count);
#endif
    if (!ps2_sb4_pad_configure(worker->app, &worker->pad) ||
        ps2_native_pad_initialize_reader(ps2_sb4_pad_read, &worker->pad)) {
        iz_video_shutdown();
        std::fprintf(stderr, "IZPS2 VM: Cannot attach libsb4 controller input.\n");
        return static_cast<uint32_t>(Ps2VmResult::InputFailed);
    }
    Ps2Sb4LiveAudio *audio = nullptr;
    const uint32_t audio_error = ps2_sb4_live_audio_attach(worker, &audio);
    Ps2VmRun result{Ps2VmResult::AudioFailed, false, "Cannot attach libsb4 AudioOut."};
    if (!audio_error) {
        settings->SetStringValue("Filenames", "BIOS", worker->media.bios_filename);
        settings->SetStringValue("SPU2/Output", "Backend", "Ounce");
        VMBootParameters boot;
        boot.filename = worker->media.iso;
        boot.fast_boot = !boot.filename.empty();
        if (!boot.fast_boot) boot.source_type = CDVD_SourceType::NoDisc;
        ModulePoll module_poll = {poll, context, input_focused};
        Ps2VmControl control = {&worker->stop_requested, poll_module, &module_poll};
#ifdef __SWITCH2__
        const auto resolve_jit = reinterpret_cast<Sb4JitCacheResolver>(compat->resolve_c(SB4_JIT_CACHE_RESOLVER));
        if (resolve_jit)
            control.jit_cache = resolve_jit();
#endif
#ifdef ENABLE_VULKAN
        const auto *profile = worker->vulkan_profile;
        const auto *graphics = reinterpret_cast<const IzNativeApp6 *>(worker->app)->graphics;
        const auto lookup = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            sb4ModuleVulkanProc(worker->app, "vkGetInstanceProcAddr"));
        const bool borrowed = graphics && lookup && profile &&
            profile->version == SB4_VULKAN_DEVICE_PROFILE_VERSION &&
            profile->bytes >= sizeof(*profile) && profile->instance_api_version >= VK_API_VERSION_1_1;
        Vulkan::BorrowedDevice device = borrowed ? Vulkan::BorrowedDevice{
            reinterpret_cast<VkInstance>(graphics->instance),
            reinterpret_cast<VkPhysicalDevice>(graphics->physical),
            reinterpret_cast<VkDevice>(graphics->device),
            reinterpret_cast<VkQueue>(graphics->queue),
            profile->queue_family_index, profile->core_features, lookup,
        } : Vulkan::BorrowedDevice{};
        if (borrowed) {
#ifdef __SWITCH2__
            iz_configure_worker_affinity("ee");
#endif
            device.record_frame = iz_video_attach_vulkan(worker->app, device.instance, lookup);
            settings->SetIntValue("EmuCore/GS", "Renderer", int(GSRendererType::VK));
            Vulkan::SetBorrowedDevice(&device);
            GSHeadless::SetEnabled(true);
        }
#endif
        result = ps2_run_vm(boot, compat->sleep_ns, &control);
#ifdef IZPS2_WRAPPER_AUDIT
        std::fprintf(stderr, "IZPS2_INPUT_TIMING polls=%llu pad_ms=%.3f focus_ms=%.3f\n",
            static_cast<unsigned long long>(module_poll.pad_polls),
            Common::Timer::ConvertValueToMilliseconds(module_poll.pad_ticks),
            Common::Timer::ConvertValueToMilliseconds(module_poll.focus_ticks));
#endif
#ifdef ENABLE_VULKAN
        if (borrowed) {
            GSHeadless::SetEnabled(false);
            Vulkan::SetBorrowedDevice(nullptr);
            settings->SetIntValue("EmuCore/GS", "Renderer", int(GSRendererType::SW));
        }
#endif
    }
    const bool audio_start_failed = audio && ps2_live_audio_startup_failed();
    const uint32_t audio_shutdown = ps2_sb4_live_audio_detach(audio);
    const uint32_t pad_error = ps2_native_pad_shutdown();
    iz_video_shutdown();
    if (!result.message.empty()) std::fprintf(stderr, "IZPS2 VM: %s\n", result.message.c_str());
    if (pad_error) {
        worker->platform_retained = 1;
        std::fprintf(stderr, "IZPS2 VM: Controller still owned (0x%08x).\n", pad_error);
        return PS2_SB4_SESSION_RETAINED;
    }
    if (audio_shutdown) return PS2_SB4_SESSION_RETAINED;
    if (audio_start_failed && result.result == Ps2VmResult::Stopped)
        return static_cast<uint32_t>(Ps2VmResult::AudioFailed);
    if (worker->video->poisoned) return static_cast<uint32_t>(Ps2VmResult::VideoFailed);
    return static_cast<uint32_t>(result.result);
}
