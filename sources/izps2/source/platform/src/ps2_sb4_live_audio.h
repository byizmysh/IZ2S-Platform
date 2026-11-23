#ifndef PS2_SB4_LIVE_AUDIO_H
#define PS2_SB4_LIVE_AUDIO_H

#include <cstdint>

struct Ps2Sb4Worker;
struct Ps2Sb4LiveAudio;

/* The worker owns the supplied arena. Detach only after SPU2 and its audio
 * thread stop. A nonzero detach leaves the owner and the arena in place. */
uint32_t ps2_sb4_live_audio_attach(Ps2Sb4Worker *, Ps2Sb4LiveAudio **);
uint32_t ps2_sb4_live_audio_detach(Ps2Sb4LiveAudio *);

#endif
