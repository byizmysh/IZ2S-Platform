#ifndef PS2_NATIVE_AUDIO_H
#define PS2_NATIVE_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_AUDIO_SLOTS 8u
#define PS2_AUDIO_SLOT_FRAMES 1024u
#define PS2_AUDIO_ARENA_BYTES (4096u+PS2_AUDIO_SLOTS*PS2_AUDIO_SLOT_FRAMES*4u)
enum Ps2AudioFunction {
    PS2_AUDIO_INIT,PS2_AUDIO_OPEN,PS2_AUDIO_RATE,PS2_AUDIO_CHANNELS,PS2_AUDIO_FORMAT,
    PS2_AUDIO_SET_BUFFER,PS2_AUDIO_APPEND,PS2_AUDIO_START,PS2_AUDIO_STOP,
    PS2_AUDIO_COUNT,PS2_AUDIO_FLUSH,PS2_AUDIO_RELEASE,PS2_AUDIO_CLOSE,
    PS2_AUDIO_FUNCTIONS
};
enum { PS2_AUDIO_LEGACY_ABI, PS2_AUDIO_SB4_ABI };
typedef struct Ps2NativeAudioConfig {
    uintptr_t functions[PS2_AUDIO_FUNCTIONS];
    void *arena; /* 4096-byte aligned, owned until a successful close. */
    size_t arena_bytes;
    void (*sleep_ns)(uint64_t);
    unsigned startup_slots; /* 1..8; 6 reproduces qualified PS1 queue depth. */
    unsigned service_abi;
} Ps2NativeAudioConfig;
typedef struct Ps2NativeAudio {
    Ps2NativeAudioConfig config;
    uint64_t accepted_frames,submitted_frames,completed_frames,appends,releases;
    uint64_t clipped_samples,backpressure,timeouts;
    uint32_t opened,started,uncertain,pending,peak_pending,format_verified;
    uint32_t write_slot,staged_frames,slot_frames[PS2_AUDIO_SLOTS];
    uint8_t busy[PS2_AUDIO_SLOTS];
    uint32_t last_error,sdk_result;
} Ps2NativeAudio;
enum Ps2AudioResult {
    PS2_AUDIO_OK=0,PS2_AUDIO_INVALID=1,PS2_AUDIO_BACKPRESSURE=2,
    PS2_AUDIO_REJECTED=3,PS2_AUDIO_SDK_ERROR=4,PS2_AUDIO_OWNERSHIP=5,
    PS2_AUDIO_TIMEOUT=6,PS2_AUDIO_UNSUPPORTED_FORMAT=7
};
/* Zero-initialize owner before the first open. One serial caller owns all APIs.
 * Same raw SDK AudioOut ABI as the proven PS1 adapter, but no 44.1kHz resampler:
 * input is exactly 48000Hz float32 interleaved stereo, output signed PCM16.
 * Opening verifies the actual SDK sample rate, channel count and sample format.
 * Source/consumed/config/owner/arena storage must satisfy the overlap checks.
 * A nonzero return never authorizes freeing an opened or uncertain arena. */
int ps2_native_audio_open(Ps2NativeAudio *,const Ps2NativeAudioConfig *);
/* Nonblocking. *consumed counts source frames copied into owned PCM storage,
 * including staged data after a rejected append. Retry only the unconsumed
 * suffix. NaN/Inf rejects the entire call before any PCM conversion/submission.
 * Samples clamp to [-1,1]; conversion truncates sample*32768 toward zero.
 * Backpressure leaves all SDK-owned descriptor/PCM bytes unchanged. */
int ps2_native_audio_feed(Ps2NativeAudio *,const float *stereo,size_t frames,size_t *consumed);
int ps2_native_audio_poll(Ps2NativeAudio *);
int ps2_native_audio_flush_partial(Ps2NativeAudio *);
/* Submit partial data, start even a short queue, wait up to wait_ms in 1ms
 * sleeps, then Stop/Flush/Count==0. A timeout keeps exact pending ownership
 * and allows later polling/drain. It does not stop, flush, close or free data.
 * Uncertain SDK responses or foreign/duplicate completions quarantine owner.
 * Feed after successful drain queues a new startup batch on the same device. */
int ps2_native_audio_drain(Ps2NativeAudio *,unsigned wait_ms);
/* Close only follows a successful drain. Repeated successful close is safe. */
int ps2_native_audio_close(Ps2NativeAudio *,unsigned wait_ms);
#ifdef __cplusplus
}
#endif
#endif
