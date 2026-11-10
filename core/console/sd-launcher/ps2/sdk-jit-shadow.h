#ifndef IZ_PS2_SDK_JIT_SHADOW_H
#define IZ_PS2_SDK_JIT_SHADOW_H
#include "jit-shadow.h"
typedef struct {uint64_t vulkan;IzPs2ShadowOps ops;} IzPs2SdkShadow;
/* Read-only current-process admission against exact R176 Vulkan bytecuts.
 * Map is a direct native six-argument call, without the PC RPC adaptor. */
uint32_t iz_ps2_sdk_shadow_bind(IzPs2SdkShadow*,const IzNativeEnvironment*,const IzNativeGraphics*);
uint32_t iz_ps2_gpu_dispatch(uint64_t vulkan,uint8_t out[4096]);
#endif
