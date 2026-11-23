#include "audio-dsp-wake-sdk.h"

#include <stdlib.h>
#include <switch/kernel/levent.h>
#include <switch/runtime/env.h>

struct IzAudioDspWake {
    LEvent event;
};

IzAudioDspWake* iz_audio_dsp_wake_create(void)
{
    // LEvent uses WaitForAddress and SignalToAddress on this SDK.
    if (!envIsSyscallHinted(0x34) || !envIsSyscallHinted(0x35))
        return NULL;

    IzAudioDspWake* wake = (IzAudioDspWake*)malloc(sizeof(*wake));
    if (wake)
        leventInit(&wake->event, false, true);
    return wake;
}

void iz_audio_dsp_wake_destroy(IzAudioDspWake* wake)
{
    free(wake);
}

void iz_audio_dsp_wake_signal(IzAudioDspWake* wake)
{
    leventSignal(&wake->event);
}

bool iz_audio_dsp_wake_wait(IzAudioDspWake* wake)
{
    return leventWait(&wake->event, 1000000);
}
