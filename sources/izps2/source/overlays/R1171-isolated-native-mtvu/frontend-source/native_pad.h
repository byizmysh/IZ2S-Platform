#ifndef PS2_NATIVE_PAD_H
#define PS2_NATIVE_PAD_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {PS2_PAD_OK=0,PS2_PAD_INVALID=0xf701,PS2_PAD_UNSUPPORTED=0xf702,PS2_PAD_BUSY=0xf703,PS2_PAD_OWNER=0xf704,PS2_PAD_IOP_MODE=0xf705,PS2_PAD_NOT_READY=0xf706};
enum {PS2_PAD_PLAYERS=4, PS2_PAD_DEVICE_AUTO=0, PS2_PAD_DEVICE_OFF=1,
      PS2_PAD_DEVICE_FIRST=2, PS2_PAD_DEVICE_HANDHELD=10, PS2_PAD_DEVICE_COUNT=11};
enum {PS2_NPAD_HANDHELD=0,PS2_NPAD_FULL=1,PS2_NPAD_DUAL=2,PS2_NPAD_LEFT=3,PS2_NPAD_RIGHT=4,PS2_NPAD_NONE=0xffffffffu};
/* Same 64B SDK state as the frozen PS1 native frontend. Y is positive up. */
typedef struct {
    uint64_t sample;
    uint32_t buttons,upper;
    int32_t lx,ly,rx,ry;
    uint32_t attributes,reserved[7];
} Ps2NativeNpadState;
typedef void (*Ps2NativeNpadGet)(Ps2NativeNpadState *,const uint32_t *);
typedef struct {Ps2NativeNpadGet get[5];} Ps2NativePadApi;
typedef uint32_t (*Ps2NativePadReader)(const void *, uint32_t, uint32_t, Ps2NativeNpadState *);
/* Canonical button bits match original PadDualshock2::Inputs[0..15].
 * Axes are clamped signed32767, positive right/down after Y inversion. */
typedef struct {
    uint64_t sample;
    uint32_t buttons;
    int32_t lx,ly,rx,ry;
    uint32_t connected,npad_id,style;
} Ps2NativePadFrame;
typedef struct {
    uint32_t busy,ready,sdk_verified,reserved;
    Ps2NativePadApi api;
    Ps2NativePadFrame frame;
    uint64_t polls,reads,switches,disconnects;
    uint32_t max_reads_per_poll,last_error;
    Ps2NativePadReader reader;
    const void *reader_context;
} Ps2NativePad;
/* Caller-owned zeroed resident storage; binding/unbinding is quiescent. No
 * thread, allocation or HID initialization is created by these functions. */
uint32_t ps2_native_pad_bind_callbacks(Ps2NativePad *,const Ps2NativePadApi *);
uint32_t ps2_native_pad_bind_reader(Ps2NativePad *, Ps2NativePadReader, const void *);
uint32_t ps2_native_pad_bind_sdk(Ps2NativePad *,uintptr_t sdk_rx,size_t sdk_bytes);
uint32_t ps2_native_pad_read(Ps2NativePad *,Ps2NativePadFrame *);
/* One persistent frame per player, initialized to zero. Explicit devices
 * reserve their IDs before automatic players are assigned. */
uint32_t ps2_native_pad_read_group(Ps2NativePad *, Ps2NativePadFrame players[PS2_PAD_PLAYERS],
                                  const uint32_t devices[PS2_PAD_PLAYERS], unsigned count);
uint32_t ps2_native_pad_unbind(Ps2NativePad *);
#ifdef __cplusplus
}
#endif
#endif
