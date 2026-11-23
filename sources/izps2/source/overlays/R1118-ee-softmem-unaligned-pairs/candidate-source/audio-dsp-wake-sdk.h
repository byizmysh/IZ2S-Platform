#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IzAudioDspWake IzAudioDspWake;
IzAudioDspWake* iz_audio_dsp_wake_create(void);
void iz_audio_dsp_wake_destroy(IzAudioDspWake* wake);
void iz_audio_dsp_wake_signal(IzAudioDspWake* wake);
bool iz_audio_dsp_wake_wait(IzAudioDspWake* wake);

#ifdef __cplusplus
}
#endif
