#include "ps2_sb4_audio.h"

int ps2_sb4_audio_bind(const IzNativeApp *app, void *arena, size_t arena_bytes,
                       Ps2NativeAudioConfig *out) {
    if (!out) return 0;
    *out = (Ps2NativeAudioConfig){0};
    const Sb4Compat *compat = sb4ModuleCompat(app);
    const IzNativeAudio *audio = sb4ModuleAudio(app);
    if (!compat || !audio || !arena || (uintptr_t)arena % 4096 ||
        arena_bytes < PS2_AUDIO_ARENA_BYTES || arena_bytes > UINTPTR_MAX - (uintptr_t)arena) return 0;

    static const unsigned slots[PS2_AUDIO_FUNCTIONS] = {
        [PS2_AUDIO_INIT] = IZ_AUDIO_INITIALIZE,
        [PS2_AUDIO_OPEN] = IZ_AUDIO_OPEN,
        [PS2_AUDIO_RATE] = IZ_AUDIO_RATE,
        [PS2_AUDIO_CHANNELS] = IZ_AUDIO_CHANNELS,
        [PS2_AUDIO_FORMAT] = IZ_AUDIO_FORMAT,
        [PS2_AUDIO_SET_BUFFER] = IZ_AUDIO_BUFFER,
        [PS2_AUDIO_APPEND] = IZ_AUDIO_APPEND,
        [PS2_AUDIO_START] = IZ_AUDIO_START,
        [PS2_AUDIO_STOP] = IZ_AUDIO_STOP,
        [PS2_AUDIO_COUNT] = IZ_AUDIO_COUNT,
        [PS2_AUDIO_FLUSH] = IZ_AUDIO_FLUSH,
        [PS2_AUDIO_RELEASE] = IZ_AUDIO_RELEASED,
        [PS2_AUDIO_CLOSE] = IZ_AUDIO_CLOSE,
    };
    for (unsigned i = 0; i < PS2_AUDIO_FUNCTIONS; ++i)
        out->functions[i] = (uintptr_t)audio->functions[slots[i]];
    out->arena = arena;
    out->arena_bytes = arena_bytes;
    out->sleep_ns = compat->sleep_ns;
    out->startup_slots = 1;
    out->service_abi = PS2_AUDIO_SB4_ABI;
    return 1;
}
