#ifndef PS2_SB4_PLATFORM_H
#define PS2_SB4_PLATFORM_H

#include "ps2_sb4_sd_saves.h"
#include "ps2_sb4_vulkan.h"
#include "ps2_sb4_pad.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const Ps2SdkFsApi *files;
    Ps2Sb4Vulkan vulkan;
    Ps2Sb4Pad pad;
} Ps2Sb4Platform;

/* Call during serialized module startup. Files are available immediately;
 * the engine initializes saves through ps2_sd_saves_native_initialize().
 * Close all engine files and join workers before native save shutdown. */
int ps2_sb4_platform_attach(IzNativeApp *app, Ps2Sb4Platform *out);

#ifdef __cplusplus
}
#endif
#endif
