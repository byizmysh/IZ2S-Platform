#ifndef PS2_SB4_AUDIO_H
#define PS2_SB4_AUDIO_H

#include <sb4/native-app.h>
#include "native_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prepare the configuration for ps2_live_audio_install. The caller supplies
 * the aligned arena and retains it until successful close and worker join. */
int ps2_sb4_audio_bind(const IzNativeApp *app, void *arena, size_t arena_bytes,
                       Ps2NativeAudioConfig *out);

#ifdef __cplusplus
}
#endif
#endif
