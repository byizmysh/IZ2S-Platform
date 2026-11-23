#ifdef ENABLE_VULKAN
#include "GS/Renderers/Vulkan/VKLoader.h"
#include "GS/Renderers/Common/GSHeadlessPresent.h"
#endif
#include "ps2_vm_runner.h"
#include "arm64/microVU_Persist-arm64.h"
namespace mVUPersist { void ConfigureHostedPersist(bool requested); }
#include "EeWriteShadow.h"
#ifdef __SWITCH2__
#include "Memory.h"
#include "common/Threading.h"

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
void iz_video_set_fps_limit(unsigned rate);
#if defined(__SWITCH2__) && defined(ENABLE_VULKAN)
#include "MTGS.h"
#include "gs-field-profile.h"
#ifdef IZPS2_GTA_EMBEDDED_SPIRV
#include "embedded-spirv.h"
#endif
namespace MTGS { void ConfigureVsyncSubmitTrace(bool enabled); void ReportVsyncSubmitTrace(); }
void iz_ps2_set_mtvu_bulk_v4(bool enabled);
void iz_ps2_configure_mtvu_resident_flags(bool enabled);
#include "ps2_mtvu_bulk_v4.hpp"
MtvuBulkV4::Counters iz_ps2_mtvu_bulk_v4_counters();
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
#if defined(IZPS2_HOSTED_WORKER) || defined(IZPS2_TEST_GTA_FRAME821) || defined(IZPS2_TEST_GTA_CHECKPOINT)
#include "SaveState.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#endif
#ifdef IZPS2_HOSTED_WORKER
#include "ps2_game_scenes.hpp"
#include "ps2_sb4_fs.h"
#include "ps2_sb4_layout.h"
#include "ps2_sb4_vm_fs.h"
#include <algorithm>
#include <vector>
#endif
#ifdef __SWITCH2__
#include "common/Switch2/CodeCache.h"
#include "native-system.h"
#include <sb4/jit-cache.h>
#include "ps2_mtvu_jit.h"
#ifdef IZPS2_HOSTED_WORKER
#include "ee-affinity-native.inc"
#endif
#include "ps2_iop_arena_probe.hpp"
#include "ps2_iop_jit_owner.hpp"
#include "EeWriteShadow.h"
#include "ps2_ee_write_lease.hpp"
#include "PINE.h"
namespace NativePoll { bool Dev9HasAsyncWriter() noexcept; }
extern "C" __attribute__((visibility("hidden"))) bool ps2_patch_has_active_dynamic_patches();
void iz_ps2_configure_live_ee_publication(bool);
void iz_ps2_configure_deferred_ee_links(bool);
void iz_ps2_configure_compact_ee_exits(bool);
void iz_ps2_report_deferred_ee_links();
bool iz_ps2_live_ee_publication_enabled();
void iz_ps2_log_live_ee_publication();
#ifdef IZPS2_SWITCH2_GTA_CADENCE_TRIAL
#include "ps2_gta_cadence_trial.hpp"
#endif
#include "IopThread/IopThreadVm.h"
#include "Memory.h"
#include "R3000A.h"
#include "ps2_mtvu_poll_gate.h"

bool recEeCheckpointEventModeMatches();
bool recEeCheckpointIopIdle()
{
    return iz_iop_jit_enabled() && IopThread::g_thread.IsRunning() &&
        IopThread::g_thread.HoldDepth() > 0;
}
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
#include "ps2_neutral_replay.h"
#include <zlib.h>
#include "ee-span-trace.hpp"
#include "ee-late-warm.hpp"
static Ps2NeutralReplay s_neutral_replay;
EeCompileSpanTrace* iz_ps2_ee_span_trace = nullptr;
ps2::ee_warm::LateWarmCapture* ps2::ee_warm::active_late_warm_capture = nullptr;
static u32 s_neutral_ee_crc;
static u32 s_neutral_ee_pc;
static u64 s_neutral_ee_cycle;
#ifdef IZPS2_SWITCH2_GTA_FRAME_WITNESS
#include "ps2_gta_frame_witness.hpp"
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

#ifdef IZPS2_HOSTED_WORKER
thread_local bool saving_game_scene = false;
#endif

#if defined(IZPS2_TEST_GTA_FRAME821) || defined(IZPS2_TEST_GTA_CHECKPOINT)
bool save_test_checkpoint(const std::string& checkpoint, bool backup) {
    std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT saving=%s\n", checkpoint.c_str());
    bool saved = true;
    VMManager::SaveState(checkpoint.c_str(), false, backup, [&](const std::string &message) {
        saved = false;
        std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT save=failed error=%s\n", message.c_str());
    });
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
#ifdef IZPS2_HOSTED_WORKER
        const auto bulk = iz_ps2_mtvu_bulk_v4_counters();
        if (bulk.enabled) {
            for (unsigned i = 0; i < 3; ++i) {
                char message[128];
                const int length = std::snprintf(message, sizeof(message),
                    "29 MTVU bulk unpack bits=%u packets=%llu rows=%llu total=%llu\n", 32u >> i,
                    static_cast<unsigned long long>(bulk.packets[i]),
                    static_cast<unsigned long long>(bulk.rows[i]),
                    static_cast<unsigned long long>(bulk.total));
                if (length > 0 && static_cast<std::size_t>(length) < sizeof(message))
                    izps2_boot_stage(nullptr, message, static_cast<uint32_t>(length));
            }
        }
#endif
#if defined(IZPS2_SWITCH2_REUSABLE_ASSEMBLER) && !defined(PS2_HOST_CODE_BRIDGE)
        izConfigureReusableBlockAssembler(false);
#endif
#ifdef __SWITCH2__
        HostSys::SetJitCacheApi(nullptr);
#endif
    }
};

struct SceneMenu {
    enum class Request { None, List, Save, Load };
    std::vector<uint32_t> ids;
    std::string status;
    size_t selection = 0;
    Request request = Request::None;
    bool open = false;
    bool available = false;
};

struct ModulePoll {
    void (*extra)(void *);
    void *context;
    Sb4InputFocusReader input_focused;
    bool paused_for_focus = false;
    Ps2Sb4Worker* worker = nullptr;
    Ps2Settings* preferences = nullptr;
    Ps2SettingsMenu settings_menu{};
    SceneMenu scenes;
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
    uint64_t pad_polls = 0, pad_failures = 0;
    Common::Timer::Value pad_ticks = 0, focus_ticks = 0;
#endif
#ifdef IZPS2_TEST_GTA_CHECKPOINT
    bool checkpoint_buttons_down = false;
    bool pad_reported = false;
    unsigned pad_id = UINT32_MAX;
    unsigned pad_style = UINT32_MAX;
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
    poll.scenes = {};
    ps2_native_pad_set_input_owned(0);
    VMManager::SetPaused(false);
}

constexpr char scene_storage_root[] = PS2_SB4_RUNTIME_DIR "/sstates/";

uint32_t list_scenes(const std::string& directory, std::vector<uint32_t>& ids, uint32_t& last_id) {
    ids.clear();
    last_id = 0;
    uint32_t token = 0;
    uint32_t result = ps2_sb4_fs_open_dir(directory.c_str(), &token);
    if (ps2_sb4_fs_is_not_found(result)) return 0;
    if (result) return result;

    Sb4StorageEntry entries[SB4_STORAGE_PAGE_MAX];
    uint32_t count = 0;
    while (!(result = ps2_sb4_fs_read_dir(token, entries, SB4_STORAGE_PAGE_MAX, &count)) && count) {
        for (uint32_t i = 0; i < count; ++i) {
            const auto& entry = entries[i];
            const uint32_t id = GameScenes::ParseFilename(entry.name);
            last_id = std::max(last_id, id);
            if (id && !entry.directory && entry.size) ids.push_back(id);
        }
    }
    const uint32_t closed = ps2_sb4_fs_close_dir(token);
    if (result || closed) {
        ids.clear();
        return result ? result : closed;
    }
    std::sort(ids.begin(), ids.end(), std::greater<uint32_t>());
    return 0;
}

std::string scene_label(uint32_t id) {
    return "SCENE " + std::to_string(id);
}

void process_scene_request(ModulePoll& poll) {
    auto& scenes = poll.scenes;
    const auto request = scenes.request;
    scenes.request = SceneMenu::Request::None;
    poll.redraw = true;
    const uint32_t disc_crc = VMManager::GetDiscCRC();
    const uint32_t game_crc = disc_crc ? disc_crc : VMManager::GetCurrentCRC();
    if (!game_crc || !VMManager::Internal::HasBootedELF()) {
        scenes.available = false;
        scenes.ids.clear();
        scenes.selection = 0;
        scenes.status = "START THE GAME TO SAVE OR LOAD SCENES";
        return;
    }

    const auto directory = GameScenes::Directory(VMManager::GetDiscSerial(), game_crc);
    const auto storage_directory = scene_storage_root + directory;
    if (request == SceneMenu::Request::List || request == SceneMenu::Request::Save) {
        uint32_t last_id = 0;
        uint32_t result = list_scenes(storage_directory, scenes.ids, last_id);
        scenes.selection = 0;
        scenes.available = result == 0;
        if (result) {
            std::fprintf(stderr, "IZPS2_SCENES list path=%s rc=0x%08x\n", storage_directory.c_str(), result);
            scenes.status = "COULD NOT READ SCENES FROM SD";
            return;
        }
        if (request == SceneMenu::Request::List) {
            scenes.status = scenes.ids.empty() ? "NO SAVED SCENES" : "";
            return;
        }
        if (last_id == UINT32_MAX) {
            scenes.status = "SCENE NUMBER LIMIT REACHED";
            return;
        }
        const uint32_t id = last_id + 1;
        const std::string path = Path::Combine(EmuFolders::Savestates, directory + "/" + GameScenes::Filename(id));
        if (!ps2_sb4_vm_fs_state_is_scoped(path.c_str())) {
            scenes.status = "SCENE STORAGE IS UNAVAILABLE";
            return;
        }
        result = ps2_sb4_fs_ensure_directory(PS2_SB4_RUNTIME_DIR "/sstates/scenes");
        if (!result) result = ps2_sb4_fs_ensure_directory(storage_directory.c_str());
        if (result) {
            std::fprintf(stderr, "IZPS2_SCENES mkdir path=%s rc=0x%08x\n", storage_directory.c_str(), result);
            scenes.status = "COULD NOT CREATE THE SCENE FOLDER";
            return;
        }
        const auto storage_path = storage_directory + "/" + GameScenes::Filename(id);
        Sb4StorageInfo info{};
        result = ps2_sb4_fs_stat(storage_path.c_str(), &info);
        if (!ps2_sb4_fs_is_not_found(result)) {
            std::fprintf(stderr, "IZPS2_SCENES new path occupied or unavailable path=%s rc=0x%08x\n",
                storage_path.c_str(), result);
            scenes.status = "COULD NOT RESERVE A NEW SCENE NUMBER";
            return;
        }
        bool saved = true;
        saving_game_scene = true;
        VMManager::SaveState(path.c_str(), false, false, [&](const std::string& error) {
            saved = false;
            std::fprintf(stderr, "IZPS2_SCENES save path=%s error=%s\n", storage_path.c_str(), error.c_str());
        });
        saving_game_scene = false;
        uint64_t bytes = 0;
        if (!saved || ps2_sb4_fs_path_size(storage_path.c_str(), &bytes) || !bytes) {
            scenes.status = "COULD NOT SAVE SCENE. CHECK SD AND MEMORY CARD";
            return;
        }
        scenes.ids.insert(scenes.ids.begin(), id);
        scenes.status = scene_label(id) + " SAVED";
        std::fprintf(stderr, "IZPS2_SCENES saved path=%s bytes=%llu\n", storage_path.c_str(),
            static_cast<unsigned long long>(bytes));
        return;
    }

    if (!scenes.selection || scenes.selection > scenes.ids.size()) return;
    const uint32_t id = scenes.ids[scenes.selection - 1];
    const auto relative = directory + "/" + GameScenes::Filename(id);
    const auto path = Path::Combine(EmuFolders::Savestates, relative);
    uint64_t bytes = 0;
    if (!ps2_sb4_vm_fs_state_is_scoped(path.c_str()) ||
        ps2_sb4_fs_path_size((scene_storage_root + relative).c_str(), &bytes) || !bytes) {
        scenes.status = "SCENE FILE IS UNAVAILABLE";
        return;
    }
    Error error;
    poll.menu_gpu_ready = false;
    if (!VMManager::LoadState(path.c_str(), &error)) {
        std::fprintf(stderr, "IZPS2_SCENES load path=%s error=%s\n", relative.c_str(),
            error.GetDescription().c_str());
        scenes.status = "COULD NOT LOAD SCENE. CHECK THE LOG";
        return;
    }
    std::fprintf(stderr, "IZPS2_SCENES loaded path=%s\n", relative.c_str());
    poll.resume_pending = true;
}

void draw_scenes(uint32_t* pixels, const SceneMenu& scenes) {
    constexpr uint32_t accent = 0xffeac45c;
    constexpr size_t visible_rows = 7;
    ps2_ui_chrome(pixels, "SCENES");
    if (scenes.available) {
        const size_t start = (scenes.selection / visible_rows) * visible_rows;
        const size_t end = std::min(start + visible_rows, scenes.ids.size() + 1);
        for (size_t row = start; row < end; ++row) {
            const unsigned y = 106 + static_cast<unsigned>(row - start) * 39;
            const bool selected = row == scenes.selection;
            if (selected) {
                ps2_ui_rect(pixels, 22, y - 8, 596, 33, 0xff493623);
                ps2_ui_rect(pixels, 22, y - 8, 3, 33, accent);
                ps2_ui_text(pixels, 38, y, ">", 2, accent, 1);
            }
            const auto label = row == 0 ? std::string("SAVE NEW SCENE") : scene_label(scenes.ids[row - 1]);
            ps2_ui_text(pixels, 70, y, label.c_str(), 2, selected ? 0xfff5f2ec : 0xffc5b8ab, 38);
        }
    }
    ps2_ui_text(pixels, 24, 385, scenes.status.c_str(), 1, accent, 85);
    const char* commands = !scenes.available ? "B BACK" : scenes.selection == 0
        ? "A SAVE    B BACK    D-PAD SELECT" : "A LOAD    B BACK    D-PAD SELECT";
    ps2_ui_text(pixels, 24, 432, commands, 1, 0xffe6d5c4, 85);
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
    else if (poll.scenes.open)
        draw_scenes(poll.menu_pixels, poll.scenes);
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
        iz_video_set_fps_limit(ps2_frame_limit_hz(poll.preferences->prefs.frame_limit));
        poll.settings_open = false;
        poll.waiting_release = true;
        if (action == PS2_SETTINGS_BOOT_BIOS) {
            poll.return_action = PS2_SB4_SESSION_BOOT_BIOS;
            module_control.stop_pending = true;
        }
        return;
    }
    if (poll.scenes.open) {
        auto& scenes = poll.scenes;
        if (pressed & (PS2_MENU_B | PS2_MENU_MINUS)) {
            scenes = {};
            poll.waiting_release = true;
            return;
        }
        if (!scenes.available) return;
        const size_t rows = scenes.ids.size() + 1;
        if (pressed & PS2_MENU_UP) scenes.selection = (scenes.selection + rows - 1) % rows;
        if (pressed & PS2_MENU_DOWN) scenes.selection = (scenes.selection + 1) % rows;
        if (pressed & PS2_MENU_A) {
            const bool save = scenes.selection == 0;
            scenes.request = save ? SceneMenu::Request::Save : SceneMenu::Request::Load;
            scenes.status = save ? "SAVING SCENE..." : "LOADING SCENE...";
            poll.waiting_release = true;
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
    if (pressed & PS2_MENU_UP) poll.selection = (poll.selection + PS2_PAUSE_ROWS - 1) % PS2_PAUSE_ROWS;
    if (pressed & PS2_MENU_DOWN) poll.selection = (poll.selection + 1) % PS2_PAUSE_ROWS;
    if (!(pressed & PS2_MENU_A)) return;
    switch (poll.selection) {
    case PS2_PAUSE_CONTINUE: poll.resume_pending = true; break;
    case PS2_PAUSE_SCENES:
        poll.scenes = {};
        poll.scenes.open = true;
        poll.scenes.request = SceneMenu::Request::List;
        poll.waiting_release = true;
        break;
    case PS2_PAUSE_SETTINGS:
        poll.settings_open = true;
        poll.settings_menu = {};
        poll.waiting_release = true;
        break;
    case PS2_PAUSE_RESTART: poll.restart_confirm = true; break;
    case PS2_PAUSE_CHOOSE_GAME:
        poll.return_action = PS2_SB4_SESSION_CHOOSE_GAME;
        module_control.stop_pending = true;
        break;
    case PS2_PAUSE_EXIT: module_control.stop_pending = true; break;
    }
}

void poll_module(void *data) {
    auto &poll = *static_cast<ModulePoll *>(data);
#ifdef IZPS2_WRAPPER_AUDIT
    const auto pad_start = Common::Timer::GetCurrentValue();
#endif
    const auto pad_result = ps2_native_pad_poll();
    const bool pad_ok = pad_result == 0;
#ifdef IZPS2_WRAPPER_AUDIT
    ++poll.pad_polls;
    poll.pad_ticks += Common::Timer::GetCurrentValue() - pad_start;
    if (!pad_ok)
        ++poll.pad_failures;
    if (poll.pad_polls == 1)
        Console.WriteLn("IZPS2_PAD_POLL first_rc=0x%08x mode=%u", pad_result,
            static_cast<unsigned>(IopThread::GetMode()));
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
    if (!poll.pad_reported || poll.connected != connected || (read_menu &&
        (poll.pad_id != frame.npad_id || poll.pad_style != frame.style))) {
        const unsigned id = read_menu ? frame.npad_id : UINT32_MAX;
        const unsigned style = read_menu ? frame.style : UINT32_MAX;
        Console.WriteLn("IZPS2_PAD_ROUTE mask=0x%x npad=%u style=%u rc=0x%08x",
            connected, id, style, pad_result);
#ifdef IZPS2_HOSTED_WORKER
        char trace[96];
        const int length = std::snprintf(trace, sizeof(trace),
            "25 pad route mask=0x%x npad=%u style=%u rc=0x%08x\n", connected, id, style, pad_result);
        if (length > 0 && static_cast<size_t>(length) < sizeof(trace))
            izps2_boot_stage(nullptr, trace, static_cast<uint32_t>(length));
#endif
        poll.pad_reported = true;
        poll.pad_id = frame.npad_id;
        poll.pad_style = frame.style;
    }
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

#ifdef IZPS2_HOSTED_WORKER
extern "C" std::unique_ptr<SaveStateScreenshotData> __real__Z24SaveState_SaveScreenshotv();
extern "C" std::unique_ptr<SaveStateScreenshotData> __wrap__Z24SaveState_SaveScreenshotv() {
    // The scenes menu is text-only. Avoid a separate GS readback while paused.
    if (saving_game_scene) return {};
    return __real__Z24SaveState_SaveScreenshotv();
}
#endif

bool ps2_vm_pump_module(bool host_stop) {
    const auto *callbacks = module_control.callbacks;
    if (!callbacks) return false;
    module_control.stop_pending = module_control.stop_pending || host_stop || stop_requested();
    using ReplayState = Ps2NeutralReplay::State;
    if (s_neutral_replay.state == ReplayState::Complete || s_neutral_replay.state == ReplayState::Discontinuity)
        module_control.stop_pending = true;
    if (!module_control.stop_pending && callbacks->poll) {
        callbacks->poll(callbacks->context);
        module_control.stop_pending = module_control.stop_pending || stop_requested();
    }
    if (module_control.stop_pending || VMManager::GetState() == VMState::Paused)
        s_neutral_replay.Interrupt();
    if (s_neutral_replay.state == ReplayState::Interrupted)
        module_control.stop_pending = true;
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

#ifdef IZPS2_HOSTED_WORKER
static bool module_flag(Ps2Sb4Worker& worker, const char* relative_path)
{
    const auto* storage = sb4ModuleStorage(worker.app);
    uint64_t size = 0;
    return storage && storage->file_size && storage->file_size(worker.app, relative_path, &size) == 0;
}

#endif

Ps2VmRun ps2_run_vm(const VMBootParameters &boot, void (*sleep_ns)(uint64_t), const Ps2VmControl *control) {
    if (!sleep_ns || (control && !control->stop_requested))
        return {Ps2VmResult::Invalid, false, "Missing PS2 VM callbacks."};
#ifdef IZPS2_GTA_EMBEDDED_SPIRV
    EmbeddedSpirv::Configure(false);
#endif
    if (vm_busy.test_and_set(std::memory_order_acquire))
        return {Ps2VmResult::Busy, false, "PS2 CPU already in use."};
    const VmOwner vm;
#ifdef IZPS2_HOSTED_WORKER
    ModulePoll* const hosted_poll = control && control->poll == poll_module
        ? static_cast<ModulePoll*>(control->context) : nullptr;
    Ps2Sb4Worker* const hosted = hosted_poll ? hosted_poll->worker : nullptr;
#endif
    ps2_reset_vu1_cpu_timing();
#if defined(IZPS2_SWITCH2_FRAME_WINDOWS) && defined(IZPS2_SWITCH2_SESSION_TIMING)
    const bool capture_stalls =
#ifdef IZPS2_HOSTED_WORKER
        hosted && module_flag(*hosted, "perf-stalls.flag");
#else
        false;
#endif
    ps2_configure_vu1_wall_timing(capture_stalls);
    const bool capture_gap_scopes =
#ifdef IZPS2_HOSTED_WORKER
        capture_stalls && hosted && module_flag(*hosted, "perf-ee-gap-scopes.flag");
#else
        false;
#endif
    const bool capture_mtgs_submit =
#ifdef IZPS2_HOSTED_WORKER
        hosted && module_flag(*hosted, "perf-mtgs-submit.flag");
#else
        false;
#endif
    MTGS::ConfigureVsyncSubmitTrace(false);
#endif
    module_control = {control, false, false};
    s_neutral_replay = {};
    for (auto& probe : s_ee_target_probes) probe = {probe.field};
    const ScopedGuard release_neutral_input([] {
        if (s_neutral_replay.state != Ps2NeutralReplay::State::Inactive)
            ps2_native_pad_set_input_owned(0);
    });
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
    bool iop_idle_entry = control && FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/iop-idle-entry.flag");
#ifdef IZPS2_HOSTED_WORKER
    if (hosted)
        iop_idle_entry = !module_flag(*hosted, "iop-idle-scalar.flag");
#endif
    ps2SetIopIdleEntryEnabled(iop_idle_entry);
    if (control) Console.WriteLn("IZPS2_IOP_IDLE_ENTRY enabled=%d", iop_idle_entry ? 1 : 0);
#endif
#ifdef IZPS2_HOSTED_WORKER
    const bool vu_cache_requested = hosted && module_flag(*hosted, "vu-split-persist.flag");
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
#ifdef IZPS2_HOSTED_WORKER
    const bool mtvu_quiet_poll = hosted && poll_latch_requested && module_flag(*hosted, "ee-mtvu-quiet-poll.flag");
    NativePoll::ConfigureMtvuQuietPoll(mtvu_quiet_poll);
    if (control)
        Console.WriteLn("IZPS2_EE_POLL_MTVU_QUIET enabled=%u", unsigned(mtvu_quiet_poll));
#endif
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
#ifdef IZPS2_HOSTED_WORKER
        // The hosted cache requires explicit admission of the sealed image.
        // With no cache, let microVU emit its ordinary non-recording code.
        {"EmuCore/CPU/Recompiler", "EnableVUProgramCache", hosted ? vu_cache_requested : true},
#else
        {"EmuCore/CPU/Recompiler", "EnableVUProgramCache", true},
#endif
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
    bool audio_dsp_worker = control && FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/audio-dsp-worker.flag");
#ifdef IZPS2_HOSTED_WORKER
    if (hosted)
        audio_dsp_worker = module_flag(*hosted, "audio-dsp-worker.flag");
#endif
    ps2_live_audio_set_stretch_worker(audio_dsp_worker);
    if (control)
        Console.WriteLn("IZPS2_AUDIO_DSP_CONFIG enabled=%u", unsigned(audio_dsp_worker));
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
#ifdef IZPS2_HOSTED_WORKER
    const bool request_mtvu = hosted && module_flag(*hosted, "mtvu-threaded.flag");
    iz_ps2_set_mtvu_bulk_v4(request_mtvu && module_flag(*hosted, "mtvu-bulk-v4.flag"));
    const bool resident_mtvu_flags = request_mtvu && module_flag(*hosted, "mtvu-resident-flags.flag");
    iz_ps2_configure_mtvu_resident_flags(resident_mtvu_flags);
    if (resident_mtvu_flags)
        Console.WriteLn("IZPS2_MTVU_RESIDENT_FLAGS requested=1");
    const bool request_iop = hosted && module_flag(*hosted, "iop-isolated-jit.flag");
#ifdef IZPS2_SWITCH2_GTA_FRAME_WITNESS
    iz_ps2_set_gta_frame_witness(false);
    const auto clear_frame_witness = ScopedGuard([] { iz_ps2_set_gta_frame_witness(false); });
    const bool request_frame_witness = hosted && module_flag(*hosted, "perf-gta-new-frames.flag");
#endif
#ifdef IZPS2_SWITCH2_GTA_CADENCE_TRIAL
    if (hosted)
        Patch::ConfigureHostedGtaCadenceTrial(module_flag(*hosted, "gta-cadence-trial.flag"));
    const ScopedGuard clear_cadence_trial([hosted] {
        if (hosted)
            Patch::ConfigureHostedGtaCadenceTrial(std::nullopt);
    });
#endif
    mVUPersist::ConfigureHostedPersist(vu_cache_requested);
    const bool observed_code_requested = hosted && module_flag(*hosted, "ee-parallel-writes.flag");
    eeWriteShadowBegin(observed_code_requested);
    const ScopedGuard end_write_shadow([observed_code_requested] {
        if (observed_code_requested)
            eeWriteShadowEnd();
    });
    const bool live_ee_stock = hosted && module_flag(*hosted, "ee-live-publication-stock.flag");
    const bool live_ee_requested = hosted && module_flag(*hosted, "ee-live-publication.flag");
    iz_ps2_configure_live_ee_publication(live_ee_requested && !live_ee_stock);
    iz_ps2_configure_deferred_ee_links(false);
    iz_ps2_configure_compact_ee_exits(false);
    s_domain_timing_active = hosted && module_flag(*hosted, "perf-ee-domains.flag");
    iz_ps2_set_ee_domain_timing(s_domain_timing_active);
    iz_ps2_set_dma_cpu_timing(hosted && module_flag(*hosted, "perf-ee-dma.flag"));
    const bool capture_iop_slice = hosted && module_flag(*hosted, "perf-iop-slice-breakdown.flag");
    iz_ps2_configure_iop_slice_breakdown(capture_iop_slice);
    const auto clear_iop_slice = ScopedGuard([] { iz_ps2_configure_iop_slice_breakdown(false); });
    const auto clear_dma_timing = ScopedGuard([] { iz_ps2_set_dma_cpu_timing(false); });
    iz_ps2_set_sd_read_timing(s_domain_timing_active);
    const ScopedGuard release_domain_timing([] {
        iz_ps2_set_ee_domain_timing(false);
        iz_ps2_set_sd_read_timing(false);
        s_domain_timing_active = false;
    });
    const ScopedGuard release_iop([] { iz_iop_jit_unconfigure(); });
    SettingsInterface* mtvu_settings = nullptr;
    bool previous_mtvu = false;
    bool had_mtvu_setting = false;
    const ScopedGuard restore_mtvu([&] {
        // CpuOwner has drained and joined VU1 before this scope ends.
        iz_mtvu_configure(nullptr, nullptr, 0);
        if (mtvu_settings) {
            const auto lock = Host::GetSettingsLock();
            if (had_mtvu_setting)
                mtvu_settings->SetBoolValue("EmuCore/Speedhacks", "vuThread", previous_mtvu);
            else
                mtvu_settings->DeleteValue("EmuCore/Speedhacks", "vuThread");
        }
    });

#endif
    const CpuOwner cpu;
#ifdef IZPS2_SWITCH2_EAGER_AB
    EagerAbTrial eager_ab;
    std::string eager_ab_checkpoint;
    const bool eager_ab_requested = control &&
        FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/ee-eager-ab.flag");
#endif
#if defined(IZPS2_SWITCH2_REUSABLE_ASSEMBLER) && !defined(PS2_HOST_CODE_BRIDGE)
    bool assembler_reuse = control && FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/jit-assembler-reuse.flag");
#ifdef IZPS2_HOSTED_WORKER
    if (hosted)
        assembler_reuse = !module_flag(*hosted, "jit-assembler-scalar.flag");
#endif
    izConfigureReusableBlockAssembler(assembler_reuse);
    if (control) Console.WriteLn("IZPS2_ASSEMBLER_REUSE enabled=%u", unsigned(assembler_reuse));
#endif
#ifdef __SWITCH2__
    HostSys::SetJitCacheApi(control ? control->jit_cache : nullptr);
#ifdef IZPS2_HOSTED_WORKER
    constexpr size_t arena_bytes = HostMemoryMap::mVU1recSize + HostMemoryMap::VIF1recSize + 4096;
    iz_mtvu_configure(request_mtvu ? hosted : nullptr, control ? control->jit_cache : nullptr, arena_bytes);
#endif
#endif
    if (control) std::fprintf(stderr, "IZPS2 VM: initializing CPU and input\n");
    if (!VMManager::Internal::CPUThreadInitialize())
        return {Ps2VmResult::CpuFailed, true, "CPU initialization failed. See native.log and emuLog.txt."};
#ifdef IZPS2_HOSTED_WORKER
    if (request_mtvu && iz_mtvu_native_is_open()) {
        const auto lock = Host::GetSettingsLock();
        mtvu_settings = Host::Internal::GetBaseSettingsLayer();
        if (mtvu_settings) {
            had_mtvu_setting = mtvu_settings->GetBoolValue("EmuCore/Speedhacks", "vuThread", &previous_mtvu);
            mtvu_settings->SetBoolValue("EmuCore/Speedhacks", "vuThread", true);
        }
    }
    if (iz_mtvu_native_is_open() && !mtvu_settings)
        return {Ps2VmResult::CpuFailed, true, "Cannot select VU1 execution mode for the isolated arena."};
    if (hosted && module_flag(*hosted, "iop-third-core-probe.flag") &&
        !iz_probe_iop_arena(*hosted, control->jit_cache, HostMemoryMap::IOPrecSize + 4096))
        return {Ps2VmResult::CpuFailed, true, "The SDK retained the IOP probe thread or arena."};
#endif
    if (stop_requested()) return {Ps2VmResult::Stopped, true, {}};
    if (control) std::fprintf(stderr, "IZPS2 VM: CPU ready, opening selected media\n");
    Error error;
    if (VMManager::Initialize(boot, &error) != VMBootResult::StartupSuccess)
        return {Ps2VmResult::BootFailed, true, error.GetDescription()};
#ifdef IZPS2_HOSTED_WORKER
    // The EE emitter still admits each block only after ELF boot and all
    // publication ownership and patch guards succeed.
    if (hosted && !live_ee_stock && !live_ee_requested &&
        VMManager::GetDiscCRC() == 0xb440a8fe && VMManager::GetDiscSerial() == "SLES-52541")
        iz_ps2_configure_live_ee_publication(true);
    if (hosted && iz_ps2_live_ee_publication_enabled() && VMManager::GetDiscCRC() == 0xb440a8fe &&
        VMManager::GetDiscSerial() == "SLES-52541" && module_flag(*hosted, "ee-deferred-links.flag"))
        iz_ps2_configure_deferred_ee_links(true);
    if (hosted && iz_ps2_live_ee_publication_enabled() && VMManager::GetDiscCRC() == 0xb440a8fe &&
        VMManager::GetDiscSerial() == "SLES-52541" && module_flag(*hosted, "ee-compact-exits.flag")) {
        iz_ps2_configure_compact_ee_exits(true);
        Console.WriteLn("IZPS2_EE_COMPACT_EXITS enabled=1");
    }
    if (iz_mtvu_native_is_open() && !THREAD_VU1) {
        VMManager::Shutdown(false);
        return {Ps2VmResult::CpuFailed, true, "Title settings did not enable the isolated VU1 worker."};
    }
    if (control)
        Console.WriteLn("IZPS2_MTVU mode=isolated requested=%u active=%u", unsigned(request_mtvu),
            unsigned(THREAD_VU1 && iz_mtvu_native_is_open()));
#endif


#ifdef IZPS2_GTA_EMBEDDED_SPIRV
    const bool use_embedded_spirv = hosted && VMManager::GetDiscCRC() == 0xb440a8fe &&
        VMManager::GetDiscSerial() == "SLES-52541" && !module_flag(*hosted, "gta-stock-spirv.flag");
    EmbeddedSpirv::Configure(use_embedded_spirv);
    if (control)
        Console.WriteLn("IZPS2_EMBEDDED_SPIRV enabled=%u", unsigned(use_embedded_spirv));
#endif
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
    const bool allow_mtvu_checkpoint =
#ifdef IZPS2_HOSTED_WORKER
        hosted && module_flag(*hosted, "ee-mtvu-checkpoint-warm.flag");
#else
        false;
#endif
    const bool prefer_stall_seed =
#ifdef IZPS2_HOSTED_WORKER
        // The embedded seed is used only after the restored checkpoint stamp
        // and each candidate block match. Keep the external seed for A/B.
        hosted && (module_flag(*hosted, "ee-checkpoint-stall-seed.flag") ||
                   !module_flag(*hosted, "ee-checkpoint-stock-seed.flag"));
#else
        false;
#endif
    const bool extend_embedded_seed =
#ifdef IZPS2_HOSTED_WORKER
        hosted && !module_flag(*hosted, "ee-checkpoint-compact-seed.flag");
#else
        false;
#endif
    const bool warm_compile_burst =
#ifdef IZPS2_HOSTED_WORKER
        hosted && !module_flag(*hosted, "ee-checkpoint-base-prefix.flag");
#else
        false;
#endif
    const bool read_burst_extension =
#ifdef IZPS2_HOSTED_WORKER
        hosted && module_flag(*hosted, "ee-checkpoint-burst-extension.flag");
#else
        false;
#endif
    const bool embedded_late_requested =
#ifdef IZPS2_HOSTED_WORKER
        hosted && !module_flag(*hosted, "ee-checkpoint-no-late-suffix.flag") &&
        !module_flag(*hosted, "perf-ee-compile-spans.flag");
#else
        false;
#endif
    const bool learn_late_requested =
#ifdef IZPS2_HOSTED_WORKER
        hosted && !module_flag(*hosted, "ee-checkpoint-no-learn-late.flag") &&
        (module_flag(*hosted, "ee-checkpoint-learn-late.flag") ||
         (embedded_late_requested && !read_burst_extension &&
          !module_flag(*hosted, "perf-neutral.flag")));
#else
        false;
#endif
    const auto prepare_ee_warm = [&](const char* phase, bool allow_fastload = false) {
#ifdef IZPS2_SWITCH2_SESSION_TIMING
        const auto before_warm = sample_session_timing();
#endif
        ee_warm = start_sb4_ee_warm(*control, allow_fastload, allow_mtvu_checkpoint, prefer_stall_seed,
                                       extend_embedded_seed, warm_compile_burst, read_burst_extension, embedded_late_requested, learn_late_requested);
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
#ifdef IZPS2_HOSTED_WORKER
    const bool neutral_requested = hosted && module_flag(*hosted, "perf-neutral.flag");
    const bool replay_requested = neutral_requested ||
        (gta && hosted && module_flag(*hosted, "gta-checkpoint-replay.flag"));
    const bool capture_requested = gta && hosted && module_flag(*hosted, "gta-checkpoint-capture.flag");
#else
    const bool replay_requested = true;
    const bool capture_requested = true;
#endif
#ifdef IZPS2_TEST_GTA_FRAME821
    constexpr unsigned checkpoint_frame = 821;
    const std::string checkpoint = Path::Combine(EmuFolders::Savestates, "izps2-r799-gta-sa-frame821.p2s");
#else
    module_control.gta_checkpoint = gta && capture_requested;
    const std::string checkpoint = Path::Combine(EmuFolders::Savestates, "izps2-r799-gta-sa-entry.p2s");
#endif
    const bool restore_checkpoint = gta && replay_requested && FileSystem::FileExists(checkpoint.c_str());
#ifdef IZPS2_HOSTED_WORKER
    if (neutral_requested && !restore_checkpoint) {
        Console.Warning("IZPS2_NEUTRAL_BENCH status=missing-gta-pal-checkpoint");
        VMManager::Shutdown(false);
        return {Ps2VmResult::BootFailed, true, "Neutral replay requires the GTA SA PAL checkpoint."};
    }
#endif
    std::unique_ptr<EeCompileSpanTrace> span_trace;
    u64 span_baseline_blocks = 0;
    const ScopedGuard clear_span_trace([] { iz_ps2_ee_span_trace = nullptr; });
#ifdef IZPS2_SWITCH2_EE_WARM
    std::unique_ptr<Sb4EeLateWarm> late_warm;
    const ScopedGuard clear_late_warm([] { ps2::ee_warm::active_late_warm_capture = nullptr; });
#endif
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
#ifdef IZPS2_HOSTED_WORKER
            if (neutral_requested) {
                ps2_native_pad_set_input_owned(1);
                ps2_native_pad_poll(); // neutralize any input already sampled at startup
                s_neutral_replay.state = Ps2NeutralReplay::State::Running;
                Console.WriteLn("IZPS2_NEUTRAL_BENCH status=armed intervals=%u", Ps2NeutralReplay::Intervals);
            }
#endif
#ifdef IZPS2_HOSTED_WORKER
#ifdef IZPS2_SWITCH2_GTA_FRAME_WITNESS
            if (request_frame_witness && VMManager::GetCurrentCRC() == 0xb440a8fe && Cpu == &recCpu) {
                iz_ps2_set_gta_frame_witness(true);
                Cpu->Reset();
                Console.WriteLn("IZPS2_GTA_FRAME_WITNESS enabled=1 crc=b440a8fe pc=0034d968");
            }
#endif
            std::optional<IopThread::VmHold> isolated_iop_hold;
            if (request_iop) {
                bool active = false;
                if (SysMemory::HasCodeMemory() && Cpu == &recCpu && psxCpu == &psxRec &&
                    !EmuConfig.Cpu.Recompiler.EnableFastmem && !IopThread::g_thread.IsRunning() &&
                    iz_iop_jit_configure(*hosted, control->jit_cache, IopThread::g_thread,
                        HostMemoryMap::IOPrecSize)) {
                    isolated_iop_hold.emplace();
                    const bool previous_iop = EmuConfig.Speedhacks.iopThread;
                    const bool previous_lockstep = EmuConfig.Speedhacks.iopThreadLockstep;
                    EmuConfig.Speedhacks.iopThread = true;
                    EmuConfig.Speedhacks.iopThreadLockstep = false;
                    IopThread::ChooseMode();
                    IopThread::SyncThread();
                    active = iz_iop_jit_view().rx != nullptr;
                    if (active) {
                        // The EE dispatcher embeds the IOP mode it was compiled for.
                        // Finish changing mode before compiling the checkpoint.
                        Cpu->Reset();
                        if (!recEeCheckpointEventModeMatches()) {
                            VMManager::Shutdown(false);
                            return {Ps2VmResult::CpuFailed, false, "Unable to select the IOP-aware EE dispatcher."};
                        }
                    } else {
                        EmuConfig.Speedhacks.iopThread = previous_iop;
                        EmuConfig.Speedhacks.iopThreadLockstep = previous_lockstep;
                        if (!IopThread::g_thread.IsRunning())
                            iz_iop_jit_unconfigure();
                        IopThread::ChooseMode();
                        IopThread::SyncThread();
                        isolated_iop_hold.reset();
                    }
                }
                Console.WriteLn("IZPS2_IOP_ISOLATED requested=1 active=%u", unsigned(active));
            }
            if (observed_code_requested) {
                extern bool g_eeSoftmemUseCanonicalMap;
                const auto iop_mode = IopThread::GetMode();
                const bool synchronous_iop = iop_mode == IopThreadMode::Off &&
                    !IopThread::g_thread.IsRunning() && !EmuConfig.Speedhacks.iopThread;
                const bool isolated_iop = iop_mode == IopThreadMode::Threaded &&
                    iz_iop_jit_view().rx && IopThread::g_thread.IsRunning() && IopThread::g_thread.IopIsStopped();
                const bool isolated_vu1 = THREAD_VU1 && iz_mtvu_native_is_open() && vu1Thread.IsDone();
                // With synchronous IOP the EE owns the SIF pumps. Isolated IOP
                // routes the same pumps through CallOnEE. Isolated MTVU work
                // runs on its VU/GS state. External writers keep exact checks.
                const bool supported = VMManager::Internal::HasBootedELF() &&
                    VMManager::GetCurrentCRC() == 0xb440a8fe && Cpu == &recCpu &&
                    !CHECK_FASTMEM && !g_eeSoftmemUseCanonicalMap && (synchronous_iop || isolated_iop) && isolated_vu1 &&
                    !ps2_patch_has_active_dynamic_patches() &&
                    !EmuConfig.Gamefixes.GoemonTlbHack && !EmuConfig.Trace.Enabled &&
                    !EmuConfig.EnablePINE && !PINEServer::IsInitialized() &&
                    !EmuConfig.DEV9.EthEnable && !EmuConfig.DEV9.HddEnable && !NativePoll::Dev9HasAsyncWriter();
                const bool observed_on = supported && eeActivateParallelObservedCode();
                if (observed_on)
                    Cpu->Reset();
                Console.WriteLn("IZPS2_EE_PARALLEL_WRITES requested=1 active=%u iop=%s",
                    unsigned(observed_on), synchronous_iop ? "synchronous" : isolated_iop ? "isolated" : "ineligible");
            }
#endif
#ifdef IZPS2_SWITCH2_EE_WARM
            if (control && sb4_ee_warm_ready(*control) &&
                !prepare_ee_warm("ee_warm_checkpoint", true)) {
#ifdef IZPS2_HOSTED_WORKER
                if (observed_code_requested)
                    eeStopEagerCodeForSettings();
#endif
                VMManager::Shutdown(false);
                return {Ps2VmResult::CpuFailed, false, "Unable to restore the normal applet clock profile."};
            }
#ifdef IZPS2_HOSTED_WORKER
            if (!ee_warm.embedded_entries.empty() && control)
            {
                late_warm = start_sb4_ee_late_warm(ee_warm, *control);
                Console.WriteLn("IZPS2_EE_LATE_WARM_ARMED active=%u stage=checkpoint cap=%u per_field=%u",
                    unsigned(bool(late_warm)), ps2::ee_warm::LateWarmCapture::Limit,
                    ps2::ee_warm::LateWarmCapture::EntriesPerBurst);
            }
#endif
#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)
            if (hosted && neutral_requested && ee_warm.read_only_seed && !read_burst_extension && !embedded_late_requested &&
                module_flag(*hosted, "perf-ee-compile-spans.flag"))
            {
                span_trace.reset(new (std::nothrow) EeCompileSpanTrace);
                if (span_trace)
                {
                    span_baseline_blocks = recGetEeCompileAudit().blocks;
                    iz_ps2_ee_span_trace = span_trace.get();
                }
                Console.WriteLn("IZPS2_EE_SPAN_TRACE armed=%u", unsigned(bool(span_trace)));
            }
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
#ifdef IZPS2_HOSTED_WORKER
            if (neutral_requested) {
                Console.Warning("IZPS2_NEUTRAL_BENCH status=load-failed");
                VMManager::Shutdown(false);
                return {Ps2VmResult::BootFailed, true, message};
            }
#endif
        }
    }
#endif

#ifdef IZPS2_TEST_GTA_FRAME821
    const bool capture_checkpoint = gta && capture_requested && !restore_checkpoint;
    VMManager::FrameAdvance(capture_checkpoint ? checkpoint_frame : 0);
    if (capture_checkpoint)
        std::fprintf(stderr, "IZPS2_GTA_CHECKPOINT armed_frame=%u\n", checkpoint_frame);
#else
    VMManager::SetState(VMState::Running);
#endif
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
    if (control) s_frame_windows.Start();
    const bool capture_nfs_gs = hosted && control && VMManager::GetDiscCRC() == 0xf5c7b45f &&
        VMManager::GetDiscSerial() == "SLUS-21065" &&
        !module_flag(*hosted, "perf-nfs-gs-fields-off.flag");
    const bool capture_gs_fields = capture_nfs_gs || (hosted &&
        s_neutral_replay.state == Ps2NeutralReplay::State::Running &&
        module_flag(*hosted, "perf-gs-fields.flag"));
    std::atomic<bool> stop_live_gs{false};
    Threading::Thread live_gs_writer;
    ScopedGuard stop_live_writer([&] {
        NfsGsLive::Enabled.store(false, std::memory_order_release);
        stop_live_gs.store(true, std::memory_order_release);
        if (live_gs_writer.Joinable())
            live_gs_writer.Join();
    });
    if (capture_nfs_gs)
    {
        const auto path = Path::Combine(EmuFolders::Logs, "izps2-r1259-nfs-live-gs.txt");
        const uint64_t session = GSFieldProfile::WallTicks();
        NfsGsLive::EeWait.End();
        NfsGsLive::GsCommand.End();
        const bool started = live_gs_writer.Start([path, session, sleep_ns, &stop_live_gs] {
            Threading::SetNameOfCurrentThread("NFS GS trace writer");
            const uint64_t hz = GetTickFrequency();
            std::string trace = "IZPS2_NFS_LIVE_HEADER session_tick=" + std::to_string(session) +
                " tick_hz=" + std::to_string(hz) + "\n";
            unsigned samples = 0;
            bool dirty = false;
            uint64_t next_save = 0;
            while (!stop_live_gs.load(std::memory_order_acquire))
            {
                const auto ee = NfsGsLive::EeWait.Read();
                const auto gs = NfsGsLive::GsCommand.Read();
                const unsigned phase = NfsGsLive::GsPhase.load(std::memory_order_relaxed);
                const auto confirm = NfsGsLive::GsCommand.Read();
                const uint64_t now = GSFieldProfile::WallTicks();
                if (samples < 192 && (ee.Elapsed(now, hz / 25) || gs.Elapsed(now, hz / 25)))
                {
                    const bool stable = gs.Active() && gs.encoded == confirm.encoded;
                    const char* phase_name = stable && phase < GSFieldProfile::Names.size() ? GSFieldProfile::Names[phase] : "unknown";
                    char row[320];
                    const int size = std::snprintf(row, sizeof(row),
                        "IZPS2_NFS_LIVE tick=%llu ee_active=%u ee_kind=%u ee_begin=%llu gs_active=%u gs_kind=%u gs_begin=%llu phase=%s\n",
                        static_cast<unsigned long long>(now), unsigned(ee.Active()), ee.Kind(),
                        static_cast<unsigned long long>(ee.Tick()), unsigned(stable), stable ? gs.Kind() : 0u,
                        static_cast<unsigned long long>(stable ? gs.Tick() : 0), phase_name);
                    if (size > 0 && size < sizeof(row))
                    {
                        trace.append(row, size);
                        ++samples;
                        dirty = true;
                    }
                }
                if (dirty && now >= next_save)
                {
                    next_save = now + hz / 3;
                    dirty = !GSCacheFile::WriteFileAtomic(path, trace.data(), trace.size(),
                        GSCacheFile::Clock::now() + std::chrono::milliseconds(150));
                }
                sleep_ns(20'000'000);
            }
            if (dirty)
                GSCacheFile::WriteFileAtomic(path, trace.data(), trace.size(),
                    GSCacheFile::Clock::now() + std::chrono::milliseconds(150));
        });
        NfsGsLive::Enabled.store(started, std::memory_order_release);
        Console.WriteLn("IZPS2_NFS_LIVE_TRACE started=%u session_tick=%llu",
            unsigned(started), static_cast<unsigned long long>(session));
    }
    if (capture_gs_fields)
    {
        MTGS::RunOnGSThread([capture_nfs_gs] { GSFieldProfile::Start(capture_nfs_gs); });
        if (capture_nfs_gs)
            GSFieldProfile::StartNfsEeWaits();
        Console.WriteLn("IZPS2_GS_TRACE_ARMED nfs=%u", unsigned(capture_nfs_gs));
    }
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    s_startup_gap.Start(control != nullptr);
    s_stall_peaks.Start(capture_stalls, Common::Timer::ConvertMillisecondsToValue(40));
    if (capture_gap_scopes) {
        s_ee_gap_trace.Start(Common::Timer::ConvertMillisecondsToValue(40));
        EeGap::active = &s_ee_gap_trace;
    }
    MTGS::ConfigureVsyncSubmitTrace(capture_mtgs_submit);
#endif
#endif
#ifdef IZPS2_BUILD_REVISION
    Host::AddOSDMessage("IZPS2 " IZPS2_BUILD_REVISION, 4.0f);
#endif
#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)
    EeAffinityTrial<EeAffinityPlatform> affinity;
    if (hosted && s_neutral_replay.state == Ps2NeutralReplay::State::Running &&
        module_flag(*hosted, "perf-ee-vu1-affinity.flag"))
    {
        const bool restrict_mask = module_flag(*hosted, "ee-exclude-vu1-core.flag");
        const auto worker = iz_ps2_vu1_cpu_ticks();
        affinity.Start(restrict_mask, worker.handle);
        Console.WriteLn("IZPS2_EE_AFFINITY requested=%u active=%u ee_mask=%llx vu1_mask=%llx selected=%llx "
                        "ee_query=0x%x vu_query=0x%x set=0x%x verify=0x%x",
            unsigned(restrict_mask), unsigned(affinity.verified),
            static_cast<unsigned long long>(affinity.saved.mask),
            static_cast<unsigned long long>(affinity.vu.mask),
            static_cast<unsigned long long>(affinity.selected.mask),
            affinity.ee_query, affinity.vu_query, affinity.set_result, affinity.verify_result);
    }
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
#ifdef IZPS2_HOSTED_WORKER
        // State I/O can replace guest memory and reset JIT providers. Run it only
        // at the paused VM owner boundary, after the input callback has returned.
        if (hosted_poll && VMManager::GetState() == VMState::Paused &&
            hosted_poll->scenes.request != SceneMenu::Request::None)
            process_scene_request(*hosted_poll);
#endif
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
            EeGap::Measure(EeGap::Phase::Guest, [] { VMManager::Execute(); });
            module_control.cpu_active = false;
            if (first_dispatch) {
                std::fprintf(stderr, "IZPS2 VM: first CPU slice returned\n");
                first_dispatch = false;
            }
        } else if (VMManager::GetState() == VMState::Paused) {
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
            s_frame_windows.Pause();
            if (ps2::ee_warm::active_late_warm_capture)
                ps2::ee_warm::active_late_warm_capture->Discontinuity();
#ifdef IZPS2_SWITCH2_SESSION_TIMING
            s_startup_gap.Pause();
            s_stall_peaks.Pause();
            if (EeGap::active) s_ee_gap_trace.Discontinuity();
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
        const auto wrapper_hz = iz_tick_frequency();
        std::fprintf(stderr, "IZPS2_WRAPPER_TIMING frames=%llu overran=%llu gs_wait_ms=%.3f vu1_wait_ms=%.3f "
            "sd_reads=%llu sd_read_bytes=%llu sd_read_ms=%.3f sd_read_errors=%llu\n",
            static_cast<unsigned long long>(tally.frames), static_cast<unsigned long long>(tally.overran_frames),
            1000.0 * tally.gs_wait_ticks / wrapper_hz,
            1000.0 * tally.vu1_wait_ticks / wrapper_hz,
            static_cast<unsigned long long>(reads.calls - start_reads.calls),
            static_cast<unsigned long long>(reads.bytes - start_reads.bytes),
            1000.0 * (reads.ticks - start_reads.ticks) / wrapper_hz,
            static_cast<unsigned long long>(reads.errors - start_reads.errors));
    }
    EECycleRateSampler::RequestTally(false);
#endif
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
    if (capture_gap_scopes) {
        EeGap::active = nullptr;
        report_ee_gap_trace();
    }
    if (capture_mtgs_submit) {
        MTGS::ReportVsyncSubmitTrace();
        MTGS::ConfigureVsyncSubmitTrace(false);
    }
    iz_ps2_ee_span_trace = nullptr;
    if (span_trace)
    {
        const auto records = span_trace->Records();
        const auto audited = recGetEeCompileAudit().blocks - span_baseline_blocks;
        const bool complete = s_neutral_replay.state == Ps2NeutralReplay::State::Complete &&
            !span_trace->Lost() && records.size() == audited;
        const auto path = Path::Combine(EmuFolders::Logs, "izps2-r1238-ee-spans.bin");
        const bool saved = complete && GSCacheFile::WriteFileAtomic(path, records.data(), records.size_bytes(),
            GSCacheFile::Clock::now() + std::chrono::milliseconds(250));
        Console.WriteLn("IZPS2_EE_SPAN_TRACE saved=%u records=%zu lost=%u audited=%llu crc=%08x",
            unsigned(saved), records.size(), span_trace->Lost(), static_cast<unsigned long long>(audited),
            crc32(0, reinterpret_cast<const u8*>(records.data()), records.size_bytes()));
    }
    if (control) report_frame_windows();
    for (const auto& probe : s_ee_target_probes) probe.Report(iz_tick_frequency());
    using ReplayState = Ps2NeutralReplay::State;
    if (s_neutral_replay.state != ReplayState::Inactive) {
        s_neutral_replay.Interrupt();
        const auto& replay = s_neutral_replay;
        const bool complete = replay.state == ReplayState::Complete;
        const char* status = complete ? "complete" :
            replay.state == ReplayState::Discontinuity ? "discontinuity" : "interrupted";
        Console.WriteLn("IZPS2_NEUTRAL_BENCH status=%s first=%u last=%u observations=%u wall_ms=%.3f "
            "ee_ram_crc=%08x ee_pc=%08x ee_cycle=%llu", status, replay.first, replay.last, replay.observations,
            Common::Timer::ConvertValueToMilliseconds(replay.last_tick - replay.first_tick),
            complete ? s_neutral_ee_crc : 0, complete ? s_neutral_ee_pc : 0,
            static_cast<unsigned long long>(complete ? s_neutral_ee_cycle : 0));
    }
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
    EECycleRateSampler::SetFrameWindowWaitTiming(false);
#endif
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
    ps2_set_ee_event_timing(false);
#endif
#ifdef IZPS2_SWITCH2_EE_WARM
    ps2::ee_warm::active_late_warm_capture = nullptr;
    if (late_warm)
        finish_sb4_ee_late_warm(*late_warm);
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
#ifdef IZPS2_HOSTED_WORKER
    iz_ps2_log_live_ee_publication();
    iz_ps2_report_deferred_ee_links();
    if (observed_code_requested) {
        Console.WriteLn("IZPS2_EE_PARALLEL_WRITES_END active=%u", unsigned(eeAllowEagerCode()));
        eeStopEagerCodeForSettings();
    }
#endif
#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)
    affinity.Restore();
    if (affinity.restore_attempted)
        Console.WriteLn("IZPS2_EE_AFFINITY_RESTORE result=0x%x restored=%u",
            affinity.restore_result, unsigned(!affinity.changed));
#endif
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
    if (capture_nfs_gs)
        GSFieldProfile::StopNfsEeWaits();
#endif
    trace_exit("26 VM shutdown begin\n");
    VMManager::Shutdown(false);
    trace_exit("27 VM shutdown returned\n");
#ifdef IZPS2_SWITCH2_FRAME_WINDOWS
    stop_live_writer.Run();
    if (capture_gs_fields)
    {
        if (MTGS::IsOpen())
        {
            Console.Warning("IZPS2_GS_TRACE_SAVED saved=0 reason=worker-open");
        }
        else
        {
            const auto report = GSFieldProfile::FormatReport();
            const auto path = Path::Combine(EmuFolders::Logs, "izps2-r1259-gs-trace.txt");
            const bool saved = GSCacheFile::WriteFileAtomic(path, report.data(), report.size(),
                GSCacheFile::Clock::now() + std::chrono::milliseconds(750));
            Console.WriteLn("IZPS2_GS_TRACE_SAVED saved=%u bytes=%zu crc=%08x",
                unsigned(saved), report.size(), static_cast<unsigned>(
                    crc32(0, reinterpret_cast<const u8*>(report.data()), report.size())));
        }
    }
#endif
#ifdef IZPS2_HOSTED_WORKER
    if (vu_cache_requested)
    {
        const auto vu = mVUPersist::GetStats(1);
        Console.WriteLn("IZPS2_VU_SPLIT_CACHE recorded=%llu dropped=%llu hydrated_programs=%llu hydrated_blocks=%llu rejected=%llu",
            static_cast<unsigned long long>(vu.chunksRecorded), static_cast<unsigned long long>(vu.chunksDropped),
            static_cast<unsigned long long>(vu.programsHydrated), static_cast<unsigned long long>(vu.blocksHydrated),
            static_cast<unsigned long long>(vu.hydrationRejects));
    }
#endif
#ifdef IZPS2_GTA_EMBEDDED_SPIRV
    if (use_embedded_spirv)
        EmbeddedSpirv::Report();
#endif
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
    iz_video_set_fps_limit(ps2_frame_limit_hz(config.frame_limit));
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
            // The borrowed device runs on the hosted SD read scope. Use the
            // in-process Vulkan cache and avoid the disk-backed startup path.
            settings->SetBoolValue("EmuCore/GS", "DisableShaderCache", true);
            Console.WriteLn("IZPS2_GS_CACHE mode=memory");
            trace_exit("20 Vulkan cache memory-only\n");
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
        std::fprintf(stderr, "IZPS2_INPUT_TIMING polls=%llu pad_failures=%llu pad_ms=%.3f focus_ms=%.3f\n",
            static_cast<unsigned long long>(module_poll.pad_polls),
            static_cast<unsigned long long>(module_poll.pad_failures),
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
