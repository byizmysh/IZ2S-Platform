#ifndef PS1_NATIVE_AUDIO_H
#define PS1_NATIVE_AUDIO_H
#include "platform.h"
/* Optional HeavyContext.reserved points here. PCM ownership remains with
 * AudioOut until the exact descriptor is released. */
typedef struct {
 uint64_t functions[13],arena,arena_bytes,sleep;
 uint64_t appends,releases,output_frames,underruns,pending,error,opened,started,uncertain;
 uint64_t empty_episodes,empty_ticks,peak_pending;
} Ps1Audio;
_Static_assert(sizeof(Ps1Audio)==224,"PS1 AudioOut ABI");
int ps1_audio_open(void);
void ps1_audio_config(unsigned volume,unsigned mute,unsigned mono,unsigned startup_buffers);
int ps1_audio_feed(const int16_t*pcm,size_t frames);
/* Once after retro_run, before video presentation: at low queue depth submit
 * ready partial PCM without waiting for the next full 1024-frame block. */
int ps1_audio_commit_frame(void);
/* Drain borrowed PCM and stop output while preserving the resampler and its
 * open device. Resume fills the normal startup queue before restarting it. */
int ps1_audio_pause(void);
/* Interactive pause/exit: stop immediately and reclaim flushed descriptors.
 * Queued sound belongs to the picture before pause and must not play later. */
int ps1_audio_pause_now(void);
int ps1_audio_pause_poll(void (*poll)(void *),void *argument);
int ps1_audio_resume(void);
int ps1_audio_close(void);
#endif
