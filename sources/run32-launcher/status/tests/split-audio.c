#define _POSIX_C_SOURCE 200112L
#include "../../native/split-audio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    INITIALIZE, OPEN, RATE, CHANNELS, FORMAT, BUFFER, APPEND,
    START, STOP, FLUSH, RELEASED, COUNT, CLOSE,
};

typedef struct {
    bool open;
    bool audible;
    bool started;
    uint64_t pending;
    uint32_t append_error;
    bool reject_mute;
    bool reject_unmute;
} Endpoint;

static uint32_t volume(void *context, unsigned slot, bool audible)
{
    Endpoint *outputs = context;
    Endpoint *output = &outputs[slot];
    assert(output->open);
    if (!audible && output->reject_mute) return 41;
    if (audible && output->reject_unmute) return 42;
    output->audible = audible;
    assert(!outputs[0].audible || !outputs[1].audible);
    return 0;
}

static uint32_t open_endpoint(void *context, unsigned slot, bool audible)
{
    Endpoint *outputs = context;
    assert(!outputs[slot].open);
    outputs[slot].open = true;
    return volume(context, slot, audible);
}

static uint32_t close_endpoint(void *context, unsigned slot)
{
    Endpoint *outputs = context;
    assert(outputs[slot].open && !outputs[slot].started && !outputs[slot].pending);
    outputs[slot] = (Endpoint){0};
    return 0;
}

static uint32_t append(void *context, unsigned slot, const IzSplitPcm *value)
{
    Endpoint *outputs = context;
    assert(!outputs[slot].pending);
    outputs[slot].pending = (uintptr_t)value;
    return outputs[slot].append_error;
}

static uint32_t start(void *context, unsigned slot)
{
    Endpoint *outputs = context;
    outputs[slot].started = true;
    return 0;
}

static uint32_t stop(void *context, unsigned slot)
{
    Endpoint *outputs = context;
    outputs[slot].started = false;
    return 0;
}

static uint32_t flush(void *context, unsigned slot, bool *flushed)
{
    Endpoint *outputs = context;
    assert(!outputs[slot].started);
    *flushed = true;
    return 0;
}

static uint32_t released(void *context, unsigned slot, uint64_t *value,
                         uint32_t *released_count)
{
    Endpoint *outputs = context;
    *value = outputs[slot].pending;
    *released_count = *value != 0;
    outputs[slot].pending = 0;
    return 0;
}

static uint32_t count(void *context, unsigned slot, uint32_t *pending_count)
{
    Endpoint *outputs = context;
    *pending_count = outputs[slot].pending != 0;
    return 0;
}

static uint64_t call(const Sb4AudioFunction *api, unsigned op, uint64_t owner)
{
    return api[op](owner, 0, 0, 0);
}

int main(int argc, char **argv)
{
    (void)argv;
    const uint64_t left_owner = 1;
    const uint64_t right_owner = 2;
    Endpoint outputs[2] = {0};
    const IzSplitAudioBackend backend = {
        open_endpoint, close_endpoint, volume, append, start, stop, flush,
        released, count,
    };
    assert(iz_split_audio_begin(&backend, outputs));
    assert(!iz_split_audio_begin(&backend, outputs));
    const Sb4AudioFunction *left = iz_split_audio_functions(IZ_SPLIT_SOUND_LEFT);
    const Sb4AudioFunction *right = iz_split_audio_functions(IZ_SPLIT_SOUND_RIGHT);
    uint64_t params = 0;
    assert(!call(left, INITIALIZE, (uintptr_t)&params));
    assert(!left[OPEN](left_owner, (uintptr_t)&params, 0, 0));
    assert(right[OPEN](left_owner, (uintptr_t)&params, 0, 0) != 0);
    assert(!right[OPEN](right_owner, (uintptr_t)&params, 0, 0));
    assert(outputs[0].open && outputs[1].open && outputs[1].audible);
    assert(!outputs[0].audible);
    assert(call(right, RATE, left_owner) == 0);
    assert(call(left, RATE, left_owner) == 48000);
    assert(call(left, CHANNELS, left_owner) == 2);
    assert(call(left, FORMAT, left_owner) == 2);
    assert(!call(left, START, left_owner) && !call(right, START, right_owner));

    void *pcm = NULL;
    assert(!posix_memalign(&pcm, 4096, 8192));
    IzSplitPcm a, b;
    assert(!left[BUFFER]((uintptr_t)&a, (uintptr_t)pcm, 4096, 1024));
    assert(!right[BUFFER]((uintptr_t)&b, (uintptr_t)pcm + 4096, 4096, 1024));

    if (argc > 1) outputs[0].append_error = 0x1234;
    assert(left[APPEND](left_owner, (uintptr_t)&a, 0, 0) ==
           (argc > 1 ? 0x1234u : 1u));
    assert(right[APPEND](right_owner, (uintptr_t)&a, 0, 0) != 1);
    assert(right[APPEND](right_owner, (uintptr_t)&b, 0, 0) == 1);
    assert(call(right, COUNT, right_owner) == 1);

    if (argc > 1) {
        assert(!call(left, RELEASED, left_owner));
        assert(call(left, CLOSE, left_owner) == 0x1234);
        assert(iz_split_audio_check_idle(IZ_SPLIT_SOUND_LEFT) == 0x1234);
        bool pending = true;
        assert(iz_split_audio_retire(IZ_SPLIT_SOUND_LEFT, &pending) == 0x1234);
        assert(!pending);
        assert(iz_split_audio_end() == 0x1234);
        assert(outputs[0].pending == (uintptr_t)&a);
        assert(call(right, RELEASED, right_owner) == (uintptr_t)&b);
        puts("PASS: uncertain IPC retains only its own endpoint and buffer");
        /* The simulation ends with both endpoints removed by process exit. */
        free(pcm);
        return 0;
    }

    outputs[1].reject_mute = true;
    assert(iz_split_audio_select(IZ_SPLIT_SOUND_LEFT) == 41);
    assert(outputs[1].audible && !outputs[0].audible);
    outputs[1].reject_mute = false;
    outputs[0].reject_unmute = true;
    assert(iz_split_audio_select(IZ_SPLIT_SOUND_LEFT) == 42);
    assert(!outputs[1].audible && !outputs[0].audible);
    outputs[0].reject_unmute = false;

    for (unsigned i = 0; i < 100; i++) {
        assert(!iz_split_audio_select(IZ_SPLIT_SOUND_LEFT));
        assert(outputs[0].audible && !outputs[1].audible);
        assert(!iz_split_audio_select(IZ_SPLIT_SOUND_RIGHT));
        assert(!outputs[0].audible && outputs[1].audible);
    }

    assert(call(left, CLOSE, left_owner));
    assert(iz_split_audio_check_idle(IZ_SPLIT_SOUND_LEFT));
    assert(!call(left, STOP, left_owner));
    assert(call(left, FLUSH, left_owner) == 1);
    assert(call(left, RELEASED, left_owner) == (uintptr_t)&a);
    assert(!call(left, CLOSE, left_owner));
    assert(!iz_split_audio_check_idle(IZ_SPLIT_SOUND_LEFT));
    assert(iz_split_audio_check_idle(IZ_SPLIT_SOUND_RIGHT));
    assert(outputs[1].pending == (uintptr_t)&b && outputs[1].audible);
    assert(outputs[1].started && outputs[1].open);

    assert(!iz_split_audio_select(IZ_SPLIT_SOUND_NONE));
    assert(!outputs[1].audible);
    assert(!call(right, STOP, right_owner));
    assert(call(right, FLUSH, right_owner) == 1);
    assert(call(right, RELEASED, right_owner) == (uintptr_t)&b);
    assert(!call(right, CLOSE, right_owner));
    assert(!iz_split_audio_end());
    assert(iz_split_audio_begin(&backend, outputs));
    assert(!iz_split_audio_end());
    free(pcm);
    puts("PASS: two native ABI outputs, focus switching, isolated release and close");
}
