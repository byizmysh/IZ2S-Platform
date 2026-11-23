#include "ps2_sb4_live_audio.h"
#include "native_audio_stream.h"
#include "ps2_sb4_session.h"

#include <sb4/native-services.h>
extern "C" {
#include <switch/kernel/thread.h>
#include <switch/kernel/svc.h>
}

#include <cstdio>
#include <memory>
#include <new>

extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);

namespace {
constexpr size_t AudioStackBytes = 256 * 1024;
// Match the RUN25 pthread provider's priority and default core selection.
constexpr int AudioPriority = 0x3b;
constexpr int DefaultProcessCore = -2;
constexpr unsigned ExitWaitMs = 1000;

struct AudioThreads {
    decltype(&threadCreate) create;
    decltype(&threadStart) start;
    decltype(&threadClose) close;
    int (*signaled)(uint32_t);
};
}

struct Ps2Sb4LiveAudio {
    Ps2Sb4Worker *worker;
    AudioThreads threads;
    Thread thread{};
    void *(*entry)(void *) = nullptr;
    void *argument = nullptr;
    bool owns_thread = false;
};

namespace {
void run_audio(void *context) {
    auto &owner = *static_cast<Ps2Sb4LiveAudio *>(context);
    owner.entry(owner.argument);
    // libounce's entry wrapper runs TLS cleanup and signals the kernel
    // thread handle after this callback returns.
}

int start(void *context, void *(*entry)(void *), void *argument, uint64_t *token) {
    auto &owner = *static_cast<Ps2Sb4LiveAudio *>(context);
    *token = 0;
    if (owner.owns_thread) return PS2_LIVE_AUDIO_THREAD_UNKNOWN;
    owner.entry = entry;
    owner.argument = argument;
    if (owner.threads.create(&owner.thread, run_audio, &owner, nullptr, AudioStackBytes,
                             AudioPriority, DefaultProcessCore)) {
        return owner.thread.handle ? PS2_LIVE_AUDIO_THREAD_UNKNOWN : 1;
    }
    owner.owns_thread = true;
    // Match libounce __syscall_thread_create: apply the process's allowed
    // core mask to the new sleeping thread before it can consume PCM.
    u64 mask = 0;
    Result affinity = svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (!affinity && mask && mask <= UINT32_MAX)
        affinity = svcSetThreadCoreMask(owner.thread.handle, -1, static_cast<u32>(mask));
    char stage[112];
    const int length = std::snprintf(stage, sizeof(stage), "21 audio process affinity mask=0x%llx result=0x%x\n",
        static_cast<unsigned long long>(mask), affinity);
    if (length > 0 && static_cast<size_t>(length) < sizeof(stage))
        izps2_boot_stage(nullptr, stage, static_cast<uint32_t>(length));
    if (owner.threads.start(&owner.thread)) {
        // A rejected Start leaves a thread object that still must be closed.
        if (owner.threads.close(&owner.thread))
            return PS2_LIVE_AUDIO_THREAD_UNKNOWN;
        owner.owns_thread = false;
        owner.thread = {};
        return 1;
    }
    *token = 1;
    return 0;
}

int join(void *context, uint64_t token) {
    auto &owner = *static_cast<Ps2Sb4LiveAudio *>(context);
    if (!owner.owns_thread || token != 1 || !owner.thread.handle)
        return PS2_LIVE_AUDIO_JOIN_UNKNOWN;

    for (unsigned waited = 0; waited <= ExitWaitMs; ++waited) {
        if (owner.threads.signaled(owner.thread.handle) == 1) {
            constexpr char stage[] = "26 audio native thread signaled\n";
            izps2_boot_stage(nullptr, stage, sizeof(stage) - 1);
            if (owner.threads.close(&owner.thread))
                return PS2_LIVE_AUDIO_JOIN_UNKNOWN;
            owner.owns_thread = false;
            owner.thread = {};
            return 0;
        }
        if (waited < ExitWaitMs)
            owner.worker->audio.sleep_ns(1000000);
    }
    constexpr char stage[] = "26 audio native thread exit timeout\n";
    izps2_boot_stage(nullptr, stage, sizeof(stage) - 1);
    return PS2_LIVE_AUDIO_JOIN_UNKNOWN;
}

void retain(void *context, int error) {
    auto &owner = *static_cast<Ps2Sb4LiveAudio *>(context);
    owner.worker->platform_retained = 1;
    std::fprintf(stderr, "IZPS2 AudioOut: retained after error %d.\n", error);
}
}

uint32_t ps2_sb4_live_audio_attach(Ps2Sb4Worker *worker, Ps2Sb4LiveAudio **out) {
    if (!out) return PS2_LIVE_AUDIO_INVALID;
    *out = nullptr;
    if (!worker || !worker->app || worker->audio.service_abi != PS2_AUDIO_SB4_ABI ||
        ps2_live_audio_installed()) return PS2_LIVE_AUDIO_INVALID;
    const Sb4Compat *compat = sb4ModuleCompat(worker->app);
    if (!compat || compat->backend != SB4_COMPAT_LIBOUNCE)
        return PS2_LIVE_AUDIO_INVALID;
    const auto version = reinterpret_cast<uint32_t (*)()>(compat->resolve_c(SB4_NATIVE_SERVICES_IMPORT));
    if (!version || version() != SB4_NATIVE_SERVICES_VERSION)
        return PS2_LIVE_AUDIO_INVALID;
    const AudioThreads threads = {
        reinterpret_cast<decltype(&threadCreate)>(compat->resolve_c("threadCreate")),
        reinterpret_cast<decltype(&threadStart)>(compat->resolve_c("threadStart")),
        reinterpret_cast<decltype(&threadClose)>(compat->resolve_c("threadClose")),
        reinterpret_cast<int (*)(uint32_t)>(compat->resolve_c("sb4HostThreadSignaled")),
    };
    if (!threads.create || !threads.start || !threads.close || !threads.signaled)
        return PS2_LIVE_AUDIO_INVALID;

    std::unique_ptr<Ps2Sb4LiveAudio> owner(new (std::nothrow) Ps2Sb4LiveAudio{worker, threads});
    if (!owner) return PS2_LIVE_AUDIO_INVALID;
    const Ps2LiveAudioHost host = {
        .audio = worker->audio, .context = owner.get(), .start_worker = start, .join_worker = join,
        .sleep_ns = worker->audio.sleep_ns, .retained = retain,
        .pull_frames = PS2_AUDIO_SLOT_FRAMES, .control_wait_ms = 1000, .drain_wait_ms = 1000,
    };
    const int result = ps2_live_audio_install(&host);
    if (!result) *out = owner.release();
    return result;
}

uint32_t ps2_sb4_live_audio_detach(Ps2Sb4LiveAudio *owner) {
    if (!owner) return 0;
    const int result = owner->owns_thread ? PS2_LIVE_AUDIO_JOIN_UNKNOWN : ps2_live_audio_uninstall();
    if (result) {
        retain(owner, result);
        return result;
    }
    delete owner;
    return 0;
}
