#pragma once
#include <stdbool.h>
#include <sb4/native-app.h>

enum { IZ_PC_PCM_SLOTS=4 };
typedef struct {
    const IzNativeAudio *audio;
    unsigned char *samples;
    uint64_t descriptors[IZ_PC_PCM_SLOTS][8];
    uint64_t parameters,stop_tick;
    uint32_t pending,failure;
    bool started,stopping;
} IzPcPcm;

uint32_t iz_pc_pcm_submit(IzPcPcm *,IzNativeApp *,const uint8_t *samples);
uint32_t iz_pc_pcm_close(IzPcPcm *,IzNativeApp *);
