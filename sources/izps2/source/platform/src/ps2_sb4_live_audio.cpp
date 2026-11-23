#include "ps2_sb4_live_audio.h"
#include "native_audio_stream.h"
#include "ps2_sb4_session.h"
#include <cstdio>
#include <memory>
#include <new>
#include <pthread.h>

struct Ps2Sb4LiveAudio {
    Ps2Sb4Worker *worker;
    pthread_t thread{};
    bool started = false;
};

namespace {
int start(void *context, void *(*entry)(void *), void *argument, uint64_t *token) {
    auto &owner = *static_cast<Ps2Sb4LiveAudio *>(context);
    *token = 0;
    if (owner.started) return 1;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes)) return 1;
    int rc = pthread_attr_setstacksize(&attributes, 256 * 1024);
    if (!rc) rc = pthread_create(&owner.thread, &attributes, entry, argument);
    pthread_attr_destroy(&attributes);
    if (rc) return 1;
    owner.started = true;
    *token = 1;
    return 0;
}

int join(void *context, uint64_t token) {
    auto &owner = *static_cast<Ps2Sb4LiveAudio *>(context);
    if (!owner.started || token != 1) return PS2_LIVE_AUDIO_JOIN_UNKNOWN;
    void *result = nullptr;
    if (pthread_join(owner.thread, &result) || result) return PS2_LIVE_AUDIO_JOIN_UNKNOWN;
    owner.started = false;
    return 0;
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
    std::unique_ptr<Ps2Sb4LiveAudio> owner(new (std::nothrow) Ps2Sb4LiveAudio{worker});
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
    int result = owner->started ? PS2_LIVE_AUDIO_JOIN_UNKNOWN : ps2_live_audio_uninstall();
    if (result) {
        retain(owner, result);
        return result;
    }
    delete owner;
    return 0;
}
