#ifndef IZ_SPLIT_AUDIO_H
#define IZ_SPLIT_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <sb4/audio.h>

typedef enum {
    IZ_SPLIT_SOUND_NONE,
    IZ_SPLIT_SOUND_LEFT,
    IZ_SPLIT_SOUND_RIGHT,
} IzSplitSoundSource;

/* ABI-compatible with the app's AudioOutBuffer descriptor. */
typedef struct {
    uint64_t next;
    uint64_t samples;
    uint64_t capacity;
    uint64_t bytes;
    uint64_t offset;
} IzSplitPcm;

typedef struct {
    uint32_t (*open)(void *, unsigned, bool);
    uint32_t (*close)(void *, unsigned);
    uint32_t (*set_audible)(void *, unsigned, bool);
    uint32_t (*append)(void *, unsigned, const IzSplitPcm *);
    uint32_t (*start)(void *, unsigned);
    uint32_t (*stop)(void *, unsigned);
    uint32_t (*flush)(void *, unsigned, bool *);
    uint32_t (*released)(void *, unsigned, uint64_t *, uint32_t *);
    uint32_t (*count)(void *, unsigned, uint32_t *);
} IzSplitAudioBackend;

/* The launcher serializes application callbacks on its frame thread. */
bool iz_split_audio_begin(const IzSplitAudioBackend *backend, void *context);
const Sb4AudioFunction *iz_split_audio_functions(IzSplitSoundSource window);
uint32_t iz_split_audio_select(IzSplitSoundSource selected);
IzSplitSoundSource iz_split_audio_source(void);
uint32_t iz_split_audio_check_idle(IzSplitSoundSource window);
/* Call only after the app acknowledges STOP. Keep its buffers alive while pending. */
uint32_t iz_split_audio_retire(IzSplitSoundSource window, bool *pending);
uint32_t iz_split_audio_end(void);
bool iz_split_audio_switch_begin(void);

#endif
