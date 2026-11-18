#pragma once
#include <stdint.h>
#define SB4_AUDIO_FUNCTIONS 13
typedef uint64_t (*Sb4AudioFunction)(uint64_t,uint64_t,uint64_t,uint64_t);
#ifdef __cplusplus
extern "C" {
#endif
/* Stable order: InitializeParameter, Open, Rate, Channels, Format,
 * BufferInfo, Append, Start, Stop, Flush, Released, Count, Close. */
const Sb4AudioFunction *sb4GetAudioFunctions(void);
uint32_t sb4CloseAudio(void);
#ifdef __cplusplus
}
#endif
