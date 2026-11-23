#ifndef PS2_NATIVE_PRESENT_BACKPRESSURE_H
#define PS2_NATIVE_PRESENT_BACKPRESSURE_H

#include "native_video.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PS2_VIDEO_BUSY = 11 };

/* Live two-buffer GS owner only. If a fence or acquire would block, preserve
 * the outstanding GPU ownership and return without submitting a picture. */
int ps2_native_video_try_present_generated_pair(Ps2NativeVideo*, unsigned width, unsigned height,
    uint32_t frame, Ps2NativeVideoGenerator, void* context);

#ifdef __cplusplus
}
#endif
#endif
