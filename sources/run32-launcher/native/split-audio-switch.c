#include "split-audio.h"

#define NX_SERVICE_ASSUME_NON_DOMAIN
#include <switch.h>

#include <stddef.h>

#define OUTPUT_ERROR 0xe870u

/* Two IAudioOut service objects with independent queues. Avoid the singleton
 * returned by audoutGetServiceSession_AudioOut(). */
static Service streams[2];

_Static_assert(sizeof(IzSplitPcm) == sizeof(AudioOutBuffer), "AudioOut descriptor");
_Static_assert(offsetof(IzSplitPcm, samples) == offsetof(AudioOutBuffer, buffer),
               "AudioOut sample pointer");

static Service *stream(void *context, unsigned slot)
{
    Service *outputs = context;
    return &outputs[slot];
}

static uint32_t set_audible(void *context, unsigned slot, bool audible)
{
    float volume = audible ? 1.0f : 0.0f;
    return serviceDispatchIn(stream(context, slot), 12, volume);
}

static uint32_t open_output(void *context, unsigned slot, bool audible)
{
    Service *output = stream(context, slot);
    if (serviceIsActive(output)) return OUTPUT_ERROR;

    Service manager = {0};
    Result result = auduOpenAudioOutManager(&manager);
    if (R_FAILED(result)) return result;

    const struct {
        u32 sample_rate;
        u32 channel_count;
        u64 resource_user_id;
    } input = { 48000, 0x00020000u, appletGetAppletResourceUserId() };
    struct {
        u32 sample_rate;
        u32 channel_count;
        u32 format;
        u32 state;
    } device = {0};
    char input_name[0x100] = {0};
    char output_name[0x100] = {0};
    result = serviceDispatchInOut(&manager, 3, input, device,
        .buffer_attrs = {
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out,
        },
        .buffers = {
            { input_name, sizeof input_name },
            { output_name, sizeof output_name },
        },
        .in_send_pid = true,
        .in_num_handles = 1,
        .in_handles = { CUR_PROCESS_HANDLE },
        .out_num_objects = 1,
        .out_objects = output);
    serviceClose(&manager);
    if (R_SUCCEEDED(result) && (device.sample_rate != 48000 ||
        device.channel_count != 2 || device.format != PcmFormat_Int16 ||
        device.state != AudioOutState_Stopped))
        result = OUTPUT_ERROR;
    if (R_SUCCEEDED(result)) result = set_audible(context, slot, audible);
    if (R_FAILED(result) && serviceIsActive(output)) serviceClose(output);
    return result;
}

static uint32_t close_output(void *context, unsigned slot)
{
    serviceClose(stream(context, slot));
    return 0;
}

static uint32_t append(void *context, unsigned slot, const IzSplitPcm *buffer)
{
    const u64 key = (uintptr_t)buffer;
    return serviceDispatchIn(stream(context, slot), 7, key,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
        .buffers = { { buffer, sizeof *buffer } });
}

static uint32_t start(void *context, unsigned slot)
{
    return serviceDispatch(stream(context, slot), 1);
}

static uint32_t stop(void *context, unsigned slot)
{
    return serviceDispatch(stream(context, slot), 2);
}

static uint32_t flush(void *context, unsigned slot, bool *flushed)
{
    u8 value = 0;
    Result result = serviceDispatchOut(stream(context, slot), 11, value);
    if (R_SUCCEEDED(result)) *flushed = (value & 1u) != 0;
    return result;
}

static uint32_t released(void *context, unsigned slot, uint64_t *descriptor,
                        uint32_t *count)
{
    return serviceDispatchOut(stream(context, slot), 8, *count,
        .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
        .buffers = { { descriptor, sizeof *descriptor } });
}

static uint32_t count(void *context, unsigned slot, uint32_t *pending)
{
    return serviceDispatchOut(stream(context, slot), 9, *pending);
}

bool iz_split_audio_switch_begin(void)
{
    static const IzSplitAudioBackend backend = {
        .open = open_output,
        .close = close_output,
        .set_audible = set_audible,
        .append = append,
        .start = start,
        .stop = stop,
        .flush = flush,
        .released = released,
        .count = count,
    };
    return iz_split_audio_begin(&backend, streams);
}
