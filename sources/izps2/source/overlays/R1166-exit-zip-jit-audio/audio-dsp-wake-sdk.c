#include "audio-dsp-wake-sdk.h"

#include <semaphore.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

struct IzAudioDspWake {
    sem_t sem;
    atomic_bool armed;
};

IzAudioDspWake* iz_audio_dsp_wake_create(void)
{
    IzAudioDspWake* wake = malloc(sizeof(*wake));
    if (!wake)
        return NULL;
    if (sem_init(&wake->sem, 0, 0)) {
        free(wake);
        return NULL;
    }
    atomic_init(&wake->armed, false);
    return wake;
}

void iz_audio_dsp_wake_destroy(IzAudioDspWake* wake)
{
    sem_destroy(&wake->sem);
    free(wake);
}

void iz_audio_dsp_wake_signal(IzAudioDspWake* wake)
{
    if (atomic_exchange_explicit(&wake->armed, false, memory_order_acq_rel))
        sem_post(&wake->sem);
}

void iz_audio_dsp_wake_arm(IzAudioDspWake* wake)
{
    // Acquire any producer exchange that preceded arming. The following
    // queue recheck then sees all work already published by that producer.
    atomic_exchange_explicit(&wake->armed, true, memory_order_acq_rel);
}

void iz_audio_dsp_wake_disarm(IzAudioDspWake* wake)
{
    atomic_store_explicit(&wake->armed, false, memory_order_release);
}

bool iz_audio_dsp_wake_wait(IzAudioDspWake* wake)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline))
        return false;
    deadline.tv_nsec += 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_nsec -= 1000000000;
        ++deadline.tv_sec;
    }
    return sem_timedwait(&wake->sem, &deadline) == 0;
}
