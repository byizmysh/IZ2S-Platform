#include "split-audio.h"

#include <stddef.h>
#include <string.h>

#define OUTPUT_ERROR 0xe870u
#define PCM_RATE 48000u
#define PCM_CHANNELS 2u
#define PCM_FORMAT_S16 2u
#define MAX_PENDING 32u

typedef struct {
    uint64_t owner;
    uint64_t pending[MAX_PENDING];
    uint32_t count;
    uint32_t failure;
    bool started;
    bool retirement_flushed;
} Output;

static struct {
    const IzSplitAudioBackend *backend;
    void *context;
    Output outputs[2];
    IzSplitSoundSource selected;
} audio;

static uint32_t retain_error(Output *output, uint32_t result)
{
    if (result && !output->failure) output->failure = result;
    return result;
}

bool iz_split_audio_begin(const IzSplitAudioBackend *backend, void *context)
{
    if (audio.backend || !backend || !backend->open || !backend->close ||
        !backend->set_audible || !backend->append || !backend->start ||
        !backend->stop || !backend->flush || !backend->released || !backend->count)
        return false;

    audio.backend = backend;
    audio.context = context;
    audio.selected = IZ_SPLIT_SOUND_RIGHT;
    return true;
}

uint32_t iz_split_audio_select(IzSplitSoundSource selected)
{
    if (!audio.backend || selected < IZ_SPLIT_SOUND_NONE ||
        selected > IZ_SPLIT_SOUND_RIGHT)
        return OUTPUT_ERROR;

    /* Muting an old output must succeed before activating the new one. */
    for (unsigned slot = 0; slot < 2; slot++) {
        if (audio.outputs[slot].owner && selected != slot + 1) {
            uint32_t result = audio.backend->set_audible(audio.context, slot, false);
            if (result) return result;
        }
    }

    /* Leave future opens silent until the unmute is confirmed. */
    audio.selected = IZ_SPLIT_SOUND_NONE;
    if (selected != IZ_SPLIT_SOUND_NONE && audio.outputs[selected - 1].owner &&
        !audio.outputs[selected - 1].retirement_flushed) {
        uint32_t result = audio.backend->set_audible(audio.context, selected - 1, true);
        if (result) return result;
    }
    audio.selected = selected;
    return 0;
}

IzSplitSoundSource iz_split_audio_source(void)
{
    return audio.selected;
}

uint32_t iz_split_audio_check_idle(IzSplitSoundSource window)
{
    if (window != IZ_SPLIT_SOUND_LEFT && window != IZ_SPLIT_SOUND_RIGHT)
        return OUTPUT_ERROR;
    const Output *output = &audio.outputs[window - 1];
    if (output->failure) return output->failure;
    return output->owner || output->count ? OUTPUT_ERROR : 0;
}

uint32_t iz_split_audio_end(void)
{
    uint32_t result = iz_split_audio_check_idle(IZ_SPLIT_SOUND_LEFT);
    if (!result) result = iz_split_audio_check_idle(IZ_SPLIT_SOUND_RIGHT);
    if (result) return result;
    memset(&audio, 0, sizeof audio);
    return 0;
}

static uint64_t initialize(uint64_t parameters, uint64_t b, uint64_t c, uint64_t d)
{
    (void)b;
    (void)c;
    (void)d;
    if (!parameters) return OUTPUT_ERROR;
    const uint32_t defaults[2] = { PCM_RATE, PCM_CHANNELS };
    memcpy((void *)(uintptr_t)parameters, defaults, sizeof defaults);
    return 0;
}

static unsigned pending_index(const Output *output, uint64_t descriptor)
{
    unsigned index = 0;
    while (index < output->count && output->pending[index] != descriptor) index++;
    return index;
}

static bool descriptor_pending(uint64_t descriptor)
{
    return pending_index(&audio.outputs[0], descriptor) < audio.outputs[0].count ||
           pending_index(&audio.outputs[1], descriptor) < audio.outputs[1].count;
}

static uint64_t buffer_info(uint64_t address, uint64_t samples, uint64_t capacity,
                            uint64_t bytes)
{
    if (!address || !samples || samples % 4096 || !capacity || capacity % 4096 ||
        bytes > capacity || descriptor_pending(address))
        return OUTPUT_ERROR;
    const IzSplitPcm buffer = { 0, samples, capacity, bytes, 0 };
    memcpy((void *)(uintptr_t)address, &buffer, sizeof buffer);
    return 0;
}

enum {
    AUDIO_INITIALIZE, AUDIO_OPEN, AUDIO_RATE, AUDIO_CHANNELS, AUDIO_FORMAT,
    AUDIO_BUFFER, AUDIO_APPEND, AUDIO_START, AUDIO_STOP, AUDIO_FLUSH,
    AUDIO_RELEASED, AUDIO_COUNT, AUDIO_CLOSE, AUDIO_COUNT_FUNCTIONS,
};
_Static_assert(AUDIO_COUNT_FUNCTIONS == SB4_AUDIO_FUNCTIONS, "native audio ABI");
_Static_assert(sizeof(IzSplitPcm) == 40, "native PCM descriptor ABI");

static uint64_t dispatch(unsigned slot, unsigned operation, uint64_t owner,
                         uint64_t argument)
{
    Output *output = &audio.outputs[slot];
    const IzSplitAudioBackend *backend = audio.backend;
    void *context = audio.context;

    if (operation == AUDIO_OPEN) {
        if (!backend || !owner || !argument || output->owner ||
            audio.outputs[1 - slot].owner == owner)
            return OUTPUT_ERROR;
        uint32_t rate;
        uint16_t channels;
        const uint8_t *parameters = (const void *)(uintptr_t)argument;
        memcpy(&rate, parameters, sizeof rate);
        memcpy(&channels, parameters + sizeof rate, sizeof channels);
        if (rate != PCM_RATE || channels != PCM_CHANNELS) return OUTPUT_ERROR;
        uint32_t result = backend->open(context, slot, audio.selected == slot + 1);
        if (!result) output->owner = owner;
        return result;
    }

    bool owns_output = backend && owner && output->owner == owner;
    if (operation == AUDIO_RATE) return owns_output ? PCM_RATE : 0;
    if (operation == AUDIO_CHANNELS) return owns_output ? PCM_CHANNELS : 0;
    if (operation == AUDIO_FORMAT) return owns_output ? PCM_FORMAT_S16 : 0;
    if (!owns_output || output->failure) {
        if (operation == AUDIO_RELEASED) return 0;
        if (operation == AUDIO_COUNT) return UINT32_MAX;
        return output->failure ? output->failure : OUTPUT_ERROR;
    }

    switch (operation) {
    case AUDIO_APPEND: {
        if (!argument || output->count == MAX_PENDING || descriptor_pending(argument))
            return OUTPUT_ERROR;
        const IzSplitPcm *buffer = (const void *)(uintptr_t)argument;
        if (!buffer->samples || buffer->samples % 4096 || !buffer->capacity ||
            buffer->capacity % 4096 || buffer->bytes > buffer->capacity || buffer->offset)
            return OUTPUT_ERROR;
        /* An IPC error may be reported after the service has used this buffer. */
        output->pending[output->count++] = argument;
        uint32_t result = retain_error(output, backend->append(context, slot, buffer));
        return result ? result : 1;
    }
    case AUDIO_START: {
        uint32_t result = backend->start(context, slot);
        if (!result) output->started = true;
        return retain_error(output, result);
    }
    case AUDIO_STOP: {
        uint32_t result = backend->stop(context, slot);
        if (!result) output->started = false;
        return retain_error(output, result);
    }
    case AUDIO_FLUSH: {
        bool flushed = false;
        uint32_t result = retain_error(output, backend->flush(context, slot, &flushed));
        return result ? result : flushed;
    }
    case AUDIO_RELEASED: {
        uint64_t descriptor = 0;
        uint32_t count = 0;
        if (retain_error(output, backend->released(context, slot, &descriptor, &count)))
            return 0;
        if (!count) return 0;
        unsigned index = pending_index(output, descriptor);
        if (count != 1 || index == output->count) {
            retain_error(output, OUTPUT_ERROR);
            return 0;
        }
        output->pending[index] = output->pending[--output->count];
        return descriptor;
    }
    case AUDIO_COUNT: {
        uint32_t count = 0;
        uint32_t result = retain_error(output, backend->count(context, slot, &count));
        return result ? UINT32_MAX : count;
    }
    case AUDIO_CLOSE: {
        if (output->started || output->count) return OUTPUT_ERROR;
        uint32_t count = 0;
        uint32_t result = backend->count(context, slot, &count);
        if (result) return retain_error(output, result);
        if (count) return OUTPUT_ERROR;
        result = backend->close(context, slot);
        if (result) return retain_error(output, result);
        memset(output, 0, sizeof *output);
        return 0;
    }
    default:
        return OUTPUT_ERROR;
    }
}

uint32_t iz_split_audio_queue_pcm(IzSplitSoundSource window, const void *owner, const IzSplitPcm *pcm)
{
    if (window != IZ_SPLIT_SOUND_LEFT && window != IZ_SPLIT_SOUND_RIGHT) return OUTPUT_ERROR;
    const unsigned slot = window - 1;
    const uint64_t owner_id = (uintptr_t)owner;
    Output *output = &audio.outputs[slot];
    if (!output->owner) {
        const uint32_t parameters[] = { PCM_RATE, PCM_CHANNELS };
        uint32_t result = dispatch(slot, AUDIO_OPEN, owner_id, (uintptr_t)parameters);
        if (result) return result;
    }
    if (output->retirement_flushed) return OUTPUT_ERROR;
    uint32_t result = dispatch(slot, AUDIO_APPEND, owner_id, (uintptr_t)pcm);
    if (output->failure) return output->failure;
    if (result != 1) return result ? result : OUTPUT_ERROR;
    return output->started ? 0 : (uint32_t)dispatch(slot, AUDIO_START, owner_id, 0);
}

uint32_t iz_split_audio_reclaim_pcm(IzSplitSoundSource window, const void *owner, uint64_t *key)
{
    if (!key || (window != IZ_SPLIT_SOUND_LEFT && window != IZ_SPLIT_SOUND_RIGHT))
        return OUTPUT_ERROR;
    const unsigned slot = window - 1;
    const Output *output = &audio.outputs[slot];
    *key = 0;
    if (!output->owner) return 0;
    if (output->owner != (uintptr_t)owner) return OUTPUT_ERROR;
    *key = dispatch(slot, AUDIO_RELEASED, (uintptr_t)owner, 0);
    return output->failure;
}

uint32_t iz_split_audio_retire(IzSplitSoundSource window, bool *pending)
{
    if (!pending || (window != IZ_SPLIT_SOUND_LEFT && window != IZ_SPLIT_SOUND_RIGHT))
        return OUTPUT_ERROR;
    *pending = false;
    unsigned slot = window - 1;
    Output *output = &audio.outputs[slot];
    if (output->failure || !output->owner)
        return iz_split_audio_check_idle(window);

    const uint64_t owner = output->owner;
    if (output->started) {
        uint32_t result = (uint32_t)dispatch(slot, AUDIO_STOP, owner, 0);
        if (result) return result;
    }
    if (!output->retirement_flushed) {
        /* The dispatch return here is a boolean on success. Check the saved
         * error separately so a successful flush is never treated as failure. */
        dispatch(slot, AUDIO_FLUSH, owner, 0);
        if (output->failure) return output->failure;
        output->retirement_flushed = true;
    }
    while (output->count && dispatch(slot, AUDIO_RELEASED, owner, 0)) {}
    if (output->failure) return output->failure;

    uint64_t queued = dispatch(slot, AUDIO_COUNT, owner, 0);
    if (output->failure) return output->failure;
    if (queued || output->count) {
        *pending = true;
        return 0;
    }
    return (uint32_t)dispatch(slot, AUDIO_CLOSE, owner, 0);
}

/* The ABI has no caller argument for several operations. Each slot gets its
 * own function table so even the same calling convention cannot mix outputs. */
#define HANDLER(slot, name, op)                                      \
    static uint64_t name##slot(uint64_t a, uint64_t b,                 \
                               uint64_t c, uint64_t d)                \
    {                                                                \
        (void)c;                                                     \
        (void)d;                                                     \
        return dispatch(slot, op, a, b);                             \
    }
#define SLOT_HANDLERS(slot)                           \
    HANDLER(slot, open, AUDIO_OPEN)                  \
    HANDLER(slot, rate, AUDIO_RATE)                  \
    HANDLER(slot, channels, AUDIO_CHANNELS)          \
    HANDLER(slot, format, AUDIO_FORMAT)              \
    HANDLER(slot, append, AUDIO_APPEND)              \
    HANDLER(slot, start, AUDIO_START)                \
    HANDLER(slot, stop, AUDIO_STOP)                  \
    HANDLER(slot, flush, AUDIO_FLUSH)                \
    HANDLER(slot, released, AUDIO_RELEASED)          \
    HANDLER(slot, count, AUDIO_COUNT)                \
    HANDLER(slot, close, AUDIO_CLOSE)
SLOT_HANDLERS(0)
SLOT_HANDLERS(1)
#undef SLOT_HANDLERS
#undef HANDLER

#define SLOT_TABLE(n) {                                                         \
    initialize, open##n, rate##n, channels##n, format##n, buffer_info,            \
    append##n, start##n, stop##n, flush##n, released##n, count##n, close##n        \
}

const Sb4AudioFunction *iz_split_audio_functions(IzSplitSoundSource window)
{
    static const Sb4AudioFunction tables[2][SB4_AUDIO_FUNCTIONS] = {
        SLOT_TABLE(0),
        SLOT_TABLE(1),
    };
    if (window != IZ_SPLIT_SOUND_LEFT && window != IZ_SPLIT_SOUND_RIGHT)
        return NULL;
    return tables[window - 1];
}
#undef SLOT_TABLE
