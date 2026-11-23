#ifndef PS2_NATIVE_AUDIO_STREAM_H
#define PS2_NATIVE_AUDIO_STREAM_H
#include "native_audio.h"
#include <memory>
class AudioStream;
class Error;
struct AudioStreamParameters;

/* Install before VM/SPU2 initialization, with no existing stream. One default
 * AudioOut and one serialized control caller. The arena and callbacks remain
 * resident until a successful shutdown, including on failed startup/join.
 * start_worker: 0 = created, 1 = definitely not created, other = uncertain.
 * join_worker: 0 = definitely joined; any other result retains everything.
 * Callbacks must implement a genuine independent worker, never inline entry.
 * audio.startup_slots must be 1: idle partial chunks can start without waiting
 * indefinitely for six complete buffers. No fallback or resampler is supplied.
 */
struct Ps2LiveAudioHost {
    Ps2NativeAudioConfig audio;
    void* context;
    int (*start_worker)(void*,void*(*)(void*),void*,uint64_t*);
    int (*join_worker)(void*,uint64_t);
    void (*sleep_ns)(uint64_t); /* thread-safe control/worker wait */
    void (*retained)(void*,int); /* optional notification; never frees storage */
    unsigned pull_frames;       /* 1..1024; persistent float staging capacity */
    unsigned control_wait_ms;   /* 1..10000 */
    unsigned drain_wait_ms;     /* 0..10000 */
};
enum Ps2LiveAudioResult {
    PS2_LIVE_AUDIO_OK=0,PS2_LIVE_AUDIO_INVALID=100,
    PS2_LIVE_AUDIO_THREAD_UNKNOWN=101,PS2_LIVE_AUDIO_JOIN_UNKNOWN=102,
    PS2_LIVE_AUDIO_CONTROL_TIMEOUT=103,PS2_LIVE_AUDIO_RETAINED=104
};
struct Ps2LiveAudioStatus {
    unsigned active,worker_created,worker_done,worker_joined,closed,retained;
    int last_error;
    uint64_t pulls,pulled_frames,staging_retries,pause_acknowledgements;
    uint64_t accepted_frames,submitted_frames,completed_frames,appends,releases;
    unsigned pending,uncertain;
};
int ps2_live_audio_install(const Ps2LiveAudioHost*);
bool ps2_live_audio_installed();
bool ps2_live_audio_startup_failed();
int ps2_live_audio_uninstall();
std::unique_ptr<AudioStream> ps2_live_audio_create(unsigned sample_rate,
    const AudioStreamParameters&,bool stretch_enabled,Error*);
/* Serialized control caller, after the SPU2 producer is quiescent. The owner
 * submits the exact staged suffix, drains remaining ring frames, then closes
 * and exits. Free
 * only after return 0 and status closed+joined. Failed shutdown retains the
 * object/arena/code. A destructor cannot report failure: it parks forever if
 * ownership or join is unknown, rather than freeing a live AudioStream ring.
 */
int ps2_live_audio_shutdown(AudioStream*);
/* Accept only the exact still-alive object returned by this factory. */
bool ps2_live_audio_status(AudioStream*,Ps2LiveAudioStatus*);
/* Exact --wrap symbols for the isolated native factory/control installation.
 * Every producer WriteChunk/EndWrite and every AudioStream processing routine
 * remains original. Wrappers serialize setters with the ring consumer.
 * EmptyBuffer retires accepted native PCM and discards a pulled float suffix
 * on the worker before resetting the original ring. The producer must already
 * be quiescent. No old native PCM remains queued when EmptyBuffer returns.
 */
#endif
