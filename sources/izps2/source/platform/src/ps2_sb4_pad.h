#ifndef PS2_SB4_PAD_H
#define PS2_SB4_PAD_H

#include "native_pad.h"
#include <sb4/input.h>
#include <sb4/native-app.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { Sb4PadReader read; } Ps2Sb4Pad;

/* Configure before starting the emulator worker. Retain the source through
 * shutdown. The worker initializes its existing PAD frontend with
 * ps2_native_pad_initialize_reader(ps2_sb4_pad_read, source). */
int ps2_sb4_pad_configure(const IzNativeApp *app, Ps2Sb4Pad *source);
uint32_t ps2_sb4_pad_read(const void *source, uint32_t style, uint32_t id, Ps2NativeNpadState *out);

#ifdef __cplusplus
}
#endif
#endif
