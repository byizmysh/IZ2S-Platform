#ifndef IZ_PS2_SDK_VIDEO_H
#define IZ_PS2_SDK_VIDEO_H
#include "video.h"
typedef struct {
 uint32_t bound,reserved;
 uint64_t functions[IZ_PS2_VIDEO_FUNCTIONS];
 IzPs2VideoOps ops;
} IzPs2SdkVideo;
/* Borrow bootstrap-qualified graphics entry points; independently qualify the
 * exact original vkGetInstanceProcAddr cut and locally resolve the two named
 * semaphore operations. Every executable span is checked in the current
 * Vulkan image with fresh PID guards. No guessed address or PC request.
 * Bind into a zero-initialized, retained controller owner once. */
uint32_t iz_ps2_sdk_video_bind(IzPs2SdkVideo*,const IzNativeEnvironment*,const IzNativeGraphics*);
#endif
