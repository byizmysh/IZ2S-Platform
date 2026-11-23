#ifdef ENABLE_VULKAN
#include "GS/Renderers/Vulkan/VKLoader.h"
#include "GS/Renderers/Common/GSHeadlessPresent.h"
#endif
#include "ps2_vm_runner.h"
#include "EeWriteShadow.h"
#ifdef __SWITCH2__
#include "Memory.h"
#include "common/Threading.h"
#include "ps2_iop_jit_lease.hpp"
#endif
#include "ps2_sb4_session.h"
#ifdef PS2_EE_DISPATCH_AB
#include "ps2_sb4_layout.h"
#include "arm64/iR5900-arm64.h"
#include "common/FileSystem.h"
#endif
#include "native_pad_frontend.h"
#include "ps2_sb4_preferences.h"
#include "ps2_psx_settings_menu.h"
#include "ps2_psx_ui.h"
#include "SIO/Pad/Pad.h"
#include "native_audio_stream.h"

extern "C" void ps2_live_audio_set_stretch_worker(bool enabled);
#include "ps2_sb4_live_audio.h"
#include "CDVD/CDVDcommon.h"
#include "Host.h"
#include "MTVU.h"
#include "R5900.h"
#include "SIO/Pad/PadDualshock2.h"
#include "VMManager.h"
#include "IopThread/IopThreadMode.h"
#ifdef IZPS2_BUILD_REVISION
#include "common/Console.h"
#endif
#include "common/Error.h"
#include "common/MemorySettingsInterface.h"
#include "common/Threading.h"
#include "common/Timer.h"
#include "video.h"
#if defined(__SWITCH2__) && defined(ENABLE_VULKAN)
#include "MTGS.h"
#endif
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
#ifdef __SWITCH2__
void recConfigureEeSmcLiteralTrial(bool enabled);
void recConfigureEeSmcInlineTrial(bool enabled);
void recConfigureEeSmcAddressPairTrial(bool enabled);
#ifdef IZPS2_SWITCH2_FRAME_END_PACING
void izConfigureFrameEndPacing(bool enabled);
#endif
void recConfigureEePcLiteralTrial(bool enabled);
#ifdef IZPS2_SWITCH2_IOP_IDLE_ENTRY
void ps2SetIopIdleEntryEnabled(bool enabled);
#endif
#endif
#ifdef IZPS2_SWITCH2_STARTUP_TRACE
#include "ps2_sb4_layout.h"
#include "common/FileSystem.h"
void izConfigureEeStartupTrace(bool enabled);
#ifdef IZPS2_SWITCH2_VU_STARTUP_TRACE
void izConfigureVuStartupTrace(bool enabled);
#endif
#endif
#ifdef PS2_EE_SOFTMEM_FPR_PAIRS
namespace R5900::Dynarec::OpcodeImpl {
void recSetFprMemoryPairing(bool enabled);
}
#endif
#include <atomic>
#include <cstdio>
#include <cstdlib>

#ifdef IZPS2_SWITCH2_LIMITER_AUDIT
// The EE thread is the only caller. Measure the full limiter call so this
// includes both waiting and its bookkeeping without changing the deadline.
static uint64_t limiter_ticks;
static uint64_t limiter_calls;

extern "C" void __real__ZN9VMManager8Internal8ThrottleEb(bool vsync_start);

extern "C" void __wrap__ZN9VMManager8Internal8ThrottleEb(bool vsync_start)
{
    const uint64_t start = iz_system_ticks();
    __real__ZN9VMManager8Internal8ThrottleEb(vsync_start);
    const uint64_t end = iz_system_ticks();
    if (end >= start)
        limiter_ticks += end - start;
    ++limiter_calls;
}
#endif

#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_FRAME_WINDOWS)
static bool s_hosted_guest_vblank;
static unsigned s_hosted_startup_fields;
extern "C" void izps2_boot_stage(IzNativeApp *, const char *, uint32_t);
constexpr bool s_defer_hosted_warm = true;
#else
constexpr bool s_defer_hosted_warm = false;
static bool s_hosted_guest_vblank = true;
#endif

#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
#include "common/Console.h"
#ifdef IZPS2_SWITCH2_EAGER_AB
#include "Memory.h"
#include "common/Path.h"
#include "ps2_eager_ab_frames.hpp"
#include <zlib.h>
#endif
#include "ps2_frame_windows.inc"
#endif

#ifdef IZPS2_SWITCH2_EE_WARM
#include "Memory.h"
#include "GS/GSCacheFile.h"
#include "common/Console.h"
#include "native_ee_warm_retain.hpp"
u32 recEeWarmBatch(std::span<const ps2::ee_warm::Entry>);
u32 recEeWarmOrderedBatch(std::span<const ps2::ee_warm::Entry>);
void recEeStartWarmProfileWithLimit(u32, u32, u32);
size_t recEeFinishWarmProfile(const std::string&, std::span<u8>);
#include "ps2_sb4_ee_warm.inc"
#endif

#if defined(IZPS2_SWITCH2_REUSABLE_ASSEMBLER) && !defined(PS2_HOST_CODE_BRIDGE)
void izConfigureReusableBlockAssembler(bool enabled);
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
#ifdef IZPS2_SWITCH2_EE_WARM
    bool ee_warm_started = false;
#endif
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

template <size_t Length>
void trace_exit(const char (&message)[Length]) {
#ifdef IZPS2_HOSTED_WORKER
    izps2_boot_stage(nullptr, message, Length - 1);
#endif
}

struct CpuOwner {
    ~CpuOwner() {
        trace_exit("28 CPU providers shutdown begin\n");
        VMManager::Internal::CPUThreadShutdown();
        trace_exit("29 CPU providers shutdown returned\n");
        vu1Thread.Close();
#if defined(IZPS2_SWITCH2_REUSABLE_ASSEMBLER) && !defined(PS2_HOST_CODE_BRIDGE)
        izConfigureReusableBlockAssembler(false);
#endif
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
    Ps2Sb4Worker* worker = nullptr;
    Ps2Settings* preferences = nullptr;
    Ps2SettingsMenu settings_menu{};
    uint32_t* menu_pixels = nullptr;
    uint32_t previous = 0;
    uint32_t menu_frame = 0;
    uint32_t return_action = 0;
    unsigned selection = 0;
    unsigned connected = 0;
    bool menu_open = false;
    bool settings_open = false;
    bool restart_confirm = false;
    bool resume_pending = false;
    bool waiting_release = false;
    bool redraw = false;
    bool menu_gpu_ready = false;
    ~ModulePoll() { std::free(menu_pixels); }
#ifdef IZPS2_WRAPPER_AUDIT
    uint64_t pad_polls = 0;
    Common::Timer::Value pad_ticks = 0, focus_ticks = 0;
#endif
#ifdef IZPS2_TEST_GTA_CHECKPOINT
    bool checkpoint_buttons_down = false;
#endif
};

uint32_t menu_buttons(const Ps2NativePadFrame& frame) {
    static constexpr unsigned game_inputs[] = {
        PadDualshock2::PAD_CIRCLE, PadDualshock2::PAD_CROSS, PadDualshock2::PAD_TRIANGLE,
        PadDualshock2::PAD_SQUARE, PadDualshock2::PAD_L1, PadDualshock2::PAD_R1,
        PadDualshock2::PAD_L2, PadDualshock2::PAD_R2, 16, PadDualshock2::PAD_LEFT,
        PadDualshock2::PAD_UP, PadDualshock2::PAD_RIGHT, PadDualshock2::PAD_DOWN
    };
    static constexpr uint32_t actions[] = {
        PS2_MENU_A, PS2_MENU_B, PS2_MENU_X, PS2_MENU_Y, PS2_MENU_L, PS2_MENU_R,
        PS2_MENU_ZL, PS2_MENU_ZR, PS2_MENU_MINUS, PS2_MENU_LEFT, PS2_MENU_UP,
        PS2_MENU_RIGHT, PS2_MENU_DOWN
    };
    uint32_t result = 0;
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
        if (frame.buttons & (1u << game_inputs[i])) result |= actions[i];
    if (frame.style == PS2_NPAD_LEFT || frame.style == PS2_NPAD_RIGHT) {
        if (frame.lx < -12000) result |= PS2_MENU_LEFT;
        if (frame.lx > 12000) result |= PS2_MENU_RIGHT;
        if (frame.ly < -12000) result |= PS2_MENU_UP;
        if (frame.ly > 12000) result |= PS2_MENU_DOWN;
    }
    return result;
}

void resume_menu(ModulePoll& poll) {
    poll.menu_open = false;
    poll.menu_gpu_ready = false;
    poll.resume_pending = false;
    ps2_native_pad_set_input_owned(0);
    VMManager::SetPaused(false);
}

void draw_native_pause(ModulePoll& poll) {
    if (module_control.cpu_active || !poll.redraw || !poll.menu_open) return;
#ifdef ENABLE_VULKAN
    if (!poll.menu_gpu_ready) {
        MTGS::WaitGS(false);
        const auto wait_idle = reinterpret_cast<PFN_vkQueueWaitIdle>(
            sb4ModuleVulkanProc(poll.worker->app, "vkQueueWaitIdle"));
        if (!wait_idle || wait_idle(reinterpret_cast<VkQueue>(poll.worker->video->queue)) != VK_SUCCESS) {
            std::fprintf(stderr, "IZPS2 pause menu: presentation queue unavailable.\n");
            resume_menu(poll);
            return;
        }
        poll.menu_gpu_ready = true;
    }
#endif
    const auto bytes = PS2_UI_WIDTH * PS2_UI_HEIGHT * sizeof(uint32_t);
    if (!poll.menu_pixels) poll.menu_pixels = static_cast<uint32_t*>(std::calloc(1, bytes));
    if (!poll.menu_pixels) {
        resume_menu(poll);
        return;
    }
    if (poll.settings_open)
        ps2_settings_menu_draw(poll.menu_pixels, &poll.settings_menu, poll.preferences, poll.connected);
    else
        ps2_ui_pause(poll.menu_pixels, poll.selection, poll.restart_confirm);
    poll.redraw = false;
    if (ps2_native_video_present(poll.worker->video, poll.menu_pixels, bytes, PS2_UI_WIDTH,
                                 PS2_UI_HEIGHT, PS2_UI_WIDTH * sizeof(uint32_t), ++poll.menu_frame)) {
        std::fprintf(stderr, "IZPS2 pause menu: failed to present.\n");
        if (poll.worker->video->poisoned) module_control.stop_pending = true;
        else resume_menu(poll);
    }
}

void process_native_menu(ModulePoll& poll, uint32_t buttons, uint32_t pressed) {
    if (module_control.cpu_active) return;
    if (poll.resume_pending) {
        if (!buttons) resume_menu(poll);
        return;
    }
    if (poll.waiting_release) {
        if (!buttons) poll.waiting_release = false;
        return;
    }
    if (!pressed) return;
    poll.redraw = true;
    if (poll.settings_open) {
        const auto action = ps2_settings_menu_step(&poll.settings_menu, poll.preferences, pressed);
        if (action == PS2_SETTINGS_STAY) return;
        poll.settings_menu.error = ps2_settings_save(poll.preferences, poll.worker->app);
        if (poll.settings_menu.error) return;
        poll.settings_open = false;
        poll.waiting_release = true;
        if (action == PS2_SETTINGS_BOOT_BIOS) {
            poll.return_action = PS2_SB4_SESSION_BOOT_BIOS;
            module_control.stop_pending = true;
        }
        return;
    }
    if (poll.restart_confirm) {
        if (pressed & (PS2_MENU_B | PS2_MENU_MINUS)) poll.restart_confirm = false;
        else if (pressed & PS2_MENU_A) {
            poll.return_action = PS2_SB4_SESSION_RESTART_GAME;
            module_control.stop_pending = true;
        }
        return;
    }
    if (pressed & (PS2_MENU_B | PS2_MENU_MINUS)) {
        poll.resume_pending = true;
        return;
    }
    if (pressed & PS2_MENU_UP) poll.selection = (poll.selection + 4) % 5;
    if (pressed & PS2_MENU_DOWN) poll.selection = (poll.selection + 1) % 5;
    if (!(pressed & PS2_MENU_A)) return;
    switch (poll.selection) {
    case 0: poll.resume_pending = true; break;
    case 1:
        poll.settings_open = true;
        poll.settings_menu = {};
        poll.waiting_release = true;
        break;
    case 2: poll.restart_confirm = true; break;
    case 3:
        poll.return_action = PS2_SB4_SESSION_CHOOSE_GAME;
        module_control.stop_pending = true;
        break;
    case 4: module_control.stop_pending = true; break;
    }
}

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
    Ps2NativePadFrame frame{};
    unsigned connected = 0;
    const bool read_menu = focused && pad_ok &&
        ps2_native_pad_menu_sample(&frame, &connected) == 0 && frame.connected;
    if (poll.connected != connected && poll.settings_open) poll.redraw = true;
    poll.connected = connected;
    const uint32_t buttons = !read_menu ? 0 : poll.menu_open ? menu_buttons(frame) :
        (frame.buttons & (1u << 16)) ? PS2_MENU_MINUS : 0;
    const uint32_t pressed = buttons & ~poll.previous;
    poll.previous = buttons;
#ifdef IZPS2_TEST_GTA_CHECKPOINT
    const bool checkpoint_buttons = read_menu && module_control.gta_checkpoint &&
        (frame.buttons & (1u << PadDualshock2::PAD_R3)) != 0 &&
        (frame.buttons & (1u << PadDualshock2::PAD_L3)) != 0;
    if (checkpoint_buttons && !poll.checkpoint_buttons_down && state == VMState::Running &&
        VMManager::Internal::HasBootedELF()) {
        module_control.checkpoint_pending = true;
        std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT requested=input\n");
    }
    poll.checkpoint_buttons_down = checkpoint_buttons;
#endif
    if (poll.menu_open) {
        if (!focused || !read_menu)
            poll.waiting_release = true;
        else {
            process_native_menu(poll, buttons, pressed);
            if (!module_control.stop_pending) draw_native_pause(poll);
        }
    } else if (state == VMState::Running && (pressed & PS2_MENU_MINUS)) {
        ps2_native_pad_set_input_owned(1);
        poll.menu_open = true;
        poll.menu_gpu_ready = false;
        poll.settings_open = false;
        poll.restart_confirm = false;
        poll.waiting_release = true;
        poll.selection = 0;
        poll.redraw = true;
        VMManager::SetPaused(true);
    } else if (!focused && state == VMState::Running) {
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
#ifdef IZPS2_SWITCH2_EE_WARM
        // The input pump also runs within the EE recompiler. Defer warming
        // until Execute has returned and the owner can compile safely.
        if (!module_control.stop_pending && !module_control.ee_warm_started &&
#ifdef IZPS2_SWITCH2_EAGER_AB
            !s_eager_ab &&
#endif
            state == VMState::Running && s_hosted_guest_vblank && sb4_ee_warm_ready(*callbacks))
            return_to_owner = true;
#endif
#ifdef IZPS2_TEST_GTA_CHECKPOINT
        return_to_owner |= module_control.checkpoint_pending;
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB
        if (auto* trial = module_control.softmem_trial) {
            observe_softmem_trial(*trial, state);
            return_to_owner |= trial->interrupted || trial->last.frames != 0;
        }
#endif
#ifdef IZPS2_SWITCH2_EAGER_AB
        if (auto* trial = s_eager_ab) {
            if (state != VMState::Running)
                trial->failure = "interrupted";
            else if (!trial->done && Common::Timer::ConvertValueToMilliseconds(Common::Timer::GetCurrentValue() - trial->launched) >= 180000)
                trial->failure = "deadline";
            return_to_owner |= trial->done || trial->failure;
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
    s_hosted_guest_vblank = !s_defer_hosted_warm;
#ifdef IZPS2_HOSTED_WORKER
    s_hosted_startup_fields = 0;
#endif
#ifdef IZPS2_TEST_SOFTMEM_AB
    recSetSoftmemOptimizationsForTest(false);
    SoftmemTrial softmem_trial;
#endif
    if (stop_requested()) return {Ps2VmResult::Stopped, false, {}};
    const char *hardware_error = nullptr;
    if (!VMManager::PerformEarlyHardwareChecks(&hardware_error))
        return {Ps2VmResult::HardwareFailed, false,
                hardware_error ? hardware_error : "CPU capability check failed."};

#ifdef IZPS2_SWITCH2_IOP_IDLE_ENTRY
    const bool iop_idle_entry = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/iop-idle-entry.flag");
    ps2SetIopIdleEntryEnabled(iop_idle_entry);
    if (control) Console.WriteLn("IZPS2_IOP_IDLE_ENTRY enabled=%d", iop_idle_entry ? 1 : 0);
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
    constexpr char poll_flag[] = "sdmc:" PS2_SB4_SD_ROOT "/ee-crossblock-poll.flag";
    constexpr char poll_control_flag[] = "sdmc:" PS2_SB4_SD_ROOT "/ee-crossblock-poll-control.flag";
    const auto poll_marker_exists = [control](const char* path) {
        if (!control)
            return false;
#ifdef IZPS2_HOSTED_WORKER
        // This worker does not have a process-global sdmc mount. Ask the
        // owning module's documented storage interface about this flag.
        if (control->poll == poll_module) {
            const auto* poll = static_cast<const ModulePoll*>(control->context);
            IzNativeApp* const app = poll->worker->app;
            const Sb4Storage* const storage = sb4ModuleStorage(app);
            uint64_t bytes = 0;
            constexpr size_t prefix = sizeof("sdmc:" PS2_SB4_SD_ROOT "/") - 1;
            return storage && storage->file_size && storage->file_size(app, path + prefix, &bytes) == 0;
        }
#endif
        return FileSystem::FileExists(path);
    };
    const bool poll_latch_requested = poll_marker_exists(poll_flag);
    const bool poll_latch_trial = poll_latch_requested || poll_marker_exists(poll_control_flag);
    if (control)
        Console.WriteLn("IZPS2_EE_POLL_CONFIG requested=%u trial=%u",
            unsigned(poll_latch_requested), unsigned(poll_latch_trial));
    NativePoll::ConfigureTrial(poll_latch_requested);
    struct PollTrialSetting {
        const char *section;
        const char *key;
        bool value;
        bool saved = false;
        bool present = false;
    };
    PollTrialSetting poll_settings[] = {
#ifndef IZPS2_SWITCH2_NATIVE_POLL_KEEP_TFX
        {"EmuCore/GS", "PrecompilePipelines", false},
#endif
        {"EmuCore/CPU/Recompiler", "EnableVUAot", false},
        {"EmuCore/CPU/Recompiler", "EnableVUProgramCache", true},
    };
    SettingsInterface *poll_base = nullptr;
    if (poll_latch_trial) {
        const auto lock = Host::GetSettingsLock();
        poll_base = Host::Internal::GetBaseSettingsLayer();
        if (poll_base) {
            for (auto &setting : poll_settings) {
                setting.present = poll_base->GetBoolValue(setting.section, setting.key, &setting.saved);
                poll_base->SetBoolValue(setting.section, setting.key, setting.value);
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
    ps2_live_audio_set_stretch_worker(control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/audio-dsp-worker.flag"));
#ifdef IZPS2_SWITCH2_FRAME_END_PACING
    const bool frame_end_pacing = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/frame-end-pacing.flag");
    izConfigureFrameEndPacing(frame_end_pacing);
    Console.WriteLn("IZPS2_FRAME_END_PACING enabled=%u", unsigned(frame_end_pacing));
#endif
#ifdef __SWITCH2__
    const bool smc_live_literal = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-smc-live-literal.flag");
    recConfigureEeSmcLiteralTrial(smc_live_literal);
    const bool smc_inline = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-smc-inline.flag");
    recConfigureEeSmcInlineTrial(smc_inline);
    const bool smc_address_pair = control && !smc_live_literal && !smc_inline &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-smc-address-pair.flag");
    recConfigureEeSmcAddressPairTrial(smc_address_pair);
    if (smc_address_pair) Console.WriteLn("IZPS2_EE_SMC_ADDRESS_PAIR enabled=1");
    const bool pc_literal_trial = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-pc-literal.flag");
    recConfigureEePcLiteralTrial(pc_literal_trial);
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
#ifdef PS2_EE_SOFTMEM_FPR_PAIRS
    const bool scalar_fpr_memory = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-scalar-fpr-memory.flag");
    R5900::Dynarec::OpcodeImpl::recSetFprMemoryPairing(!scalar_fpr_memory);
    if (control)
        Console.WriteLn("IZPS2_EE_FPR_MEMORY mode=%s", scalar_fpr_memory ? "scalar" : "paired");
#endif
    const CpuOwner cpu;
#ifdef IZPS2_SWITCH2_EAGER_AB
    EagerAbTrial eager_ab;
    std::string eager_ab_checkpoint;
    const bool eager_ab_requested = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-eager-ab.flag");
#endif
#if defined(IZPS2_SWITCH2_REUSABLE_ASSEMBLER) && !defined(PS2_HOST_CODE_BRIDGE)
    const bool assembler_reuse = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/jit-assembler-reuse.flag");
    izConfigureReusableBlockAssembler(assembler_reuse);
    if (control) Console.WriteLn("IZPS2_ASSEMBLER_REUSE enabled=%u", unsigned(assembler_reuse));
#endif
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
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
    const bool event_profile = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-event-profile.flag");
    ps2_set_ee_event_timing(event_profile);
    if (control) Console.WriteLn("IZPS2_EE_EVENT_PROFILE enabled=%d", event_profile ? 1 : 0);
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
    EECycleRateSampler::SetFrameWindowWaitTiming(control != nullptr);
#endif
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    if (control) Console.WriteLn("IZPS2_BUILD " IZ_JIT_BUILD_ID);
#if defined(__SWITCH2__) && defined(ENABLE_VULKAN)
    if (control)
        Console.WriteLn("IZPS2_GS_BACK_THREAD requested=%u", unsigned(EmuConfig.GS.BackThread));
#endif
#ifdef __SWITCH2__
    if (control) Console.WriteLn("IZPS2_EE_SMC_LITERAL enabled=%d", smc_live_literal ? 1 : 0);
    if (control) Console.WriteLn("IZPS2_EE_SMC_INLINE enabled=%d", smc_inline ? 1 : 0);
    if (control) Console.WriteLn("IZPS2_EE_PC_LITERAL enabled=%d", pc_literal_trial ? 1 : 0);
#endif
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
    const auto prepare_ee_warm = [&](const char* phase, bool allow_fastload = false) {
#ifdef IZPS2_SWITCH2_SESSION_TIMING
        const auto before_warm = sample_session_timing();
#endif
        ee_warm = start_sb4_ee_warm(*control, allow_fastload);
        module_control.ee_warm_started = ee_warm.disc != 0;
        if (module_control.ee_warm_started)
            Console.WriteLn("IZPS2_EE_WARM_START phase=%s", phase);
#ifdef IZPS2_SWITCH2_SESSION_TIMING
        report_session_timing(phase, 0, before_warm, sample_session_timing());
#endif
        return ee_warm.fastload_restored;
    };
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
            if (control && sb4_ee_warm_ready(*control) &&
                !prepare_ee_warm("ee_warm_checkpoint", true)) {
                VMManager::Shutdown(false);
                return {Ps2VmResult::CpuFailed, false, "Unable to restore the normal applet clock profile."};
            }
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
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    s_startup_gap.Start(control && FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/startup-gap.flag"));
#endif
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
#ifdef IZPS2_SWITCH2_EAGER_AB
        if (s_eager_ab && VMManager::GetState() != VMState::Running)
            eager_ab.failure = "interrupted";
        if (s_eager_ab && (eager_ab.done || eager_ab.failure)) {
            s_eager_ab = nullptr;
            if (!eager_ab_report(eager_ab)) {
                std::fprintf(stderr, "IZPS2_EAGER_AB_ABORT reason=%s\n",
                    eager_ab.failure ? eager_ab.failure : "invalid-window");
                break;
            }
            if (eager_ab.optimized) {
                std::fprintf(stderr, "IZPS2_EAGER_AB_DONE ee_endpoint_match=%u\n",
                    unsigned(eager_ab.digest == eager_ab.exact_digest));
                break;
            }
            eager_ab.exact_digest = eager_ab.digest;
            if (!prepare_eager_ab(true))
                break;
            VMManager::SetState(VMState::Running);
        }
#endif
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
#ifdef IZPS2_SWITCH2_EE_WARM
            if (control && !module_control.ee_warm_started &&
#ifdef IZPS2_SWITCH2_EAGER_AB
                !s_eager_ab &&
#endif
                s_hosted_guest_vblank && sb4_ee_warm_ready(*control))
                prepare_ee_warm("ee_warm_game_boot");
#endif
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
#ifdef IZPS2_SWITCH2_SESSION_TIMING
            s_startup_gap.Pause();
#endif
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
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
    EECycleRateSampler::SetFrameWindowWaitTiming(false);
#endif
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
    ps2_set_ee_event_timing(false);
#endif
#ifdef IZPS2_SWITCH2_EE_WARM
    finish_sb4_ee_warm(ee_warm);
#endif
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
    if (poll_latch_trial) NativePoll::LogTrial();
#endif
#ifdef IZPS2_SWITCH2_EAGER_AB
    if (s_eager_ab) {
        s_eager_ab = nullptr;
        std::fprintf(stderr, "IZPS2_EAGER_AB_ABORT reason=stopped\n");
    }
#endif
    trace_exit("26 VM shutdown begin\n");
    VMManager::Shutdown(false);
    trace_exit("27 VM shutdown returned\n");
#ifdef IZPS2_SWITCH2_EAGER_AB
    if (eager_ab.launched)
        ps2_native_pad_set_input_owned(0);
#endif
#if defined(__SWITCH2__) && defined(ENABLE_VULKAN)
    if (control && iz_video_has_frame_samples()) {
        // Save the opt-in reports before CpuOwner closes the file logger.
        MTGS::ShutdownThread();
        iz_video_finish_presentations();
    }
#endif
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
    Ps2Settings preferences{};
    if (const auto error = ps2_settings_load(&preferences, worker->app)) {
        std::fprintf(stderr, "IZPS2 settings read 0x%08x; using defaults.\n", error);
        preferences = {};
        preferences.prefs.volume = 100;
        for (auto& profile : preferences.prefs.profiles) profile.deadzone = 1;
    }
    const auto& config = preferences.prefs;
    unsigned ports[PS2_PAD_PLAYERS];
    const unsigned players = ps2_controller_slots(config.multitap, ports);
    settings->SetBoolValue("Pad", "MultitapPort1", config.multitap == PS2_MULTITAP_PORT_1);
    settings->SetBoolValue("Pad", "MultitapPort2", config.multitap == PS2_MULTITAP_PORT_2);
    for (unsigned player = 0; player < players; ++player) {
        const auto section = Pad::GetConfigSection(ports[player]);
        settings->SetStringValue(section.c_str(), "Type",
            Pad::GetControllerInfo(Pad::ControllerType::DualShock2)->name);
    }
    if (const auto error = ps2_native_pad_configure_players(ports, config.devices, config.profiles, players)) {
        std::fprintf(stderr, "IZPS2 controller setup 0x%08x.\n", error);
        const auto shutdown = ps2_native_pad_shutdown();
        iz_video_shutdown();
        if (shutdown) {
            worker->platform_retained = 1;
            return PS2_SB4_SESSION_RETAINED;
        }
        return static_cast<uint32_t>(Ps2VmResult::InputFailed);
    }
    static constexpr const char* aspects[] = {"Auto 4:3/3:2", "4:3", "16:9", "Stretch"};
    settings->SetStringValue("EmuCore/GS", "AspectRatio", aspects[config.aspect]);
    settings->SetIntValue("SPU2/Output", "StandardVolume", config.volume);
    settings->SetIntValue("SPU2/Output", "FastForwardVolume", config.volume);
    Ps2Sb4LiveAudio *audio = nullptr;
    const uint32_t audio_error = ps2_sb4_live_audio_attach(worker, &audio);
    uint32_t return_action = 0;
    Ps2VmRun result{Ps2VmResult::AudioFailed, false, "Cannot attach libsb4 AudioOut."};
    if (!audio_error) {
        settings->SetStringValue("Filenames", "BIOS", worker->media.bios_filename);
        settings->SetStringValue("SPU2/Output", "Backend", "Ounce");
        VMBootParameters boot;
        boot.filename = worker->media.iso;
        boot.fast_boot = !boot.filename.empty() && !config.full_boot;
        if (!boot.fast_boot) boot.source_type = CDVD_SourceType::NoDisc;
        ModulePoll module_poll = {poll, context, input_focused};
        module_poll.worker = worker;
        module_poll.preferences = &preferences;
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
        Console.WriteLn("IZPS2_GS_HANDOFF borrowed=%u graphics=%u lookup=%u profile=%u profile_bytes=%u api=%u",
            unsigned(borrowed), unsigned(graphics != nullptr), unsigned(lookup != nullptr),
            profile ? profile->version : 0u, profile ? profile->bytes : 0u,
            profile ? profile->instance_api_version : 0u);
        Vulkan::BorrowedDevice device = borrowed ? Vulkan::BorrowedDevice{
            reinterpret_cast<VkInstance>(graphics->instance),
            reinterpret_cast<VkPhysicalDevice>(graphics->physical),
            reinterpret_cast<VkDevice>(graphics->device),
            reinterpret_cast<VkQueue>(graphics->queue),
            profile->queue_family_index, profile->core_features, lookup,
        } : Vulkan::BorrowedDevice{};
        if (borrowed) {
            device.record_frame = iz_video_attach_vulkan(worker->app, device.instance, lookup);
            Console.WriteLn("IZPS2_GS_HANDOFF record_frame=%u queue_family=%u",
                            unsigned(device.record_frame != nullptr), device.queue_family_index);
            settings->SetIntValue("EmuCore/GS", "Renderer", int(GSRendererType::VK));
            Vulkan::SetBorrowedDevice(&device);
            GSHeadless::SetEnabled(true);
        }
#endif
#if defined(__SWITCH2__) && defined(ENABLE_VULKAN)
        const bool gs_back_trial = borrowed &&
            FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/gs-back-thread.flag");
        bool gs_back_saved = false;
        const bool gs_back_present = gs_back_trial &&
            settings->GetBoolValue("EmuCore/GS", "BackThread", &gs_back_saved);
        if (gs_back_trial)
            settings->SetBoolValue("EmuCore/GS", "BackThread", true);
#endif
        result = ps2_run_vm(boot, compat->sleep_ns, &control);
#if defined(__SWITCH2__) && defined(ENABLE_VULKAN)
        if (gs_back_trial) {
            if (gs_back_present)
                settings->SetBoolValue("EmuCore/GS", "BackThread", gs_back_saved);
            else
                settings->DeleteValue("EmuCore/GS", "BackThread");
        }
#endif
        return_action = module_poll.return_action;
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
    trace_exit("30 audio detach begin\n");
    const uint32_t audio_shutdown = ps2_sb4_live_audio_detach(audio);
    trace_exit("31 pad shutdown begin\n");
    const uint32_t pad_error = ps2_native_pad_shutdown();
    trace_exit("32 video shutdown begin\n");
    iz_video_shutdown();
    trace_exit("33 video shutdown returned\n");
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
    if (return_action && result.result == Ps2VmResult::Stopped) return return_action;
    return static_cast<uint32_t>(result.result);
}
