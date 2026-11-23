#ifndef SB4_STREAM_H
#define SB4_STREAM_H

#include <stddef.h>
#include <stdint.h>

struct IzNativeApp;

enum {
    SB4_STREAM_RGB565 = 0,
    SB4_STREAM_BC1 = 1,
    SB4_STREAM_INDEXED8 = 2,
    SB4_STREAM_INVALID = 0xe660,
    SB4_STREAM_PENDING = 0xe661,
    SB4_STREAM_UNAVAILABLE = 0xe662,
};

enum { SB4_STREAM_BLITBUN = 1, SB4_STREAM_VACOOTUBE = 2 };

/* Optional IZlauncher imports sb4StreamOpenPC, sb4StreamUpdatePC and
 * sb4StreamClosePC. Open during INIT, update from FRAME and close in STOP.
 * An update can report PENDING or UNAVAILABLE without ending the app. */
typedef uint32_t (*Sb4StreamOpenPC)(uint32_t source,uint64_t *token);
typedef uint32_t (*Sb4StreamUpdatePC)(struct IzNativeApp *app,uint64_t token);
typedef uint32_t (*Sb4StreamClosePC)(struct IzNativeApp *app,uint64_t token);

#ifdef __cplusplus
extern "C" {
#endif

/* Zero means this format and size are unsupported. */
size_t sb4StreamFrameBytes(uint32_t width, uint32_t height, uint32_t format);
/* BlitBun/VacooTube private video layout. Use the serialized native app
 * callback, with the launcher's presentation loop owning the GPU queue.
 * The library copies a complete frame to the inactive slot. */
uint32_t sb4StreamSubmitFrame(struct IzNativeApp *app, uint32_t width, uint32_t height,
                             uint32_t format, const void *payload, size_t bytes, uint32_t *publication);
uint32_t sb4StreamFrameAck(const struct IzNativeApp *app, uint32_t *publication);
uint32_t sb4StreamSetSourceReady(struct IzNativeApp *app, uint32_t ready);

#ifdef __cplusplus
}
#endif
#endif
