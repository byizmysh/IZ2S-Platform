#ifndef PS2_NATIVE_FRONTEND_FRAME_H
#define PS2_NATIVE_FRONTEND_FRAME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* A local GS producer and the serialized native SD FRAME callback share this
 * queue. The frontend's public framebuffer remains at the ABI's fixed address.
 * No swapchain, device, network command, timer, allocation or wait is involved.
 * The owner validates and holds the complete backing allocation until actual
 * GS/engine joins. Queue state alone never proves a worker/device is stopped. */
#define PS2_FRONTEND_FRAME_MAGIC UINT32_C(0x32464650)
#define PS2_FRONTEND_FRAME_SLOTS 3u
#define PS2_FRONTEND_FRAME_HEADER_BYTES 256u
enum {PS2_FRONTEND_LIVE=1,PS2_FRONTEND_CLOSING=2,PS2_FRONTEND_CLOSED=3,PS2_FRONTEND_POISONED=4};
enum {PS2_FRONTEND_FREE=0,PS2_FRONTEND_WRITING=1,PS2_FRONTEND_READY=2,PS2_FRONTEND_READING=3};
enum {PS2_FRONTEND_OK=0,PS2_FRONTEND_NO_FRAME=1,PS2_FRONTEND_INVALID=2,PS2_FRONTEND_BUSY=3,PS2_FRONTEND_OVERFLOW=4};
typedef struct {
 uint32_t state,width,height,guest_frame;
 uint64_t sequence,pixels_offset,capacity;
 uint64_t reserved[3];
} Ps2FrontendFrameSlot;
typedef struct {
 uint32_t magic,version,bytes,state;
 uint32_t maximum_width,maximum_height,slot_count,producer_active;
 uint64_t backing_bytes,next_sequence,consumed_sequence,reserved;
 Ps2FrontendFrameSlot slot[PS2_FRONTEND_FRAME_SLOTS];
} Ps2FrontendFrame;
typedef struct {uint32_t slot,reserved;uint64_t sequence;} Ps2FrontendFrameWrite;
/* storage is ordinary owner-held coherent RW memory, disjoint from the public
 * framebuffer and all engine/image/descriptor objects. Initialize before entry.
 * Pitch is packed RGBA; alpha normalization/menu composition belongs to the GS
 * producer before commit, exactly as for the existing generated frame. */
uint32_t ps2_frontend_frame_initialize(Ps2FrontendFrame*,size_t,unsigned,unsigned);
uint32_t ps2_frontend_frame_begin(Ps2FrontendFrame*,Ps2FrontendFrameWrite*,void**,size_t*);
uint32_t ps2_frontend_frame_commit(Ps2FrontendFrame*,const Ps2FrontendFrameWrite*,unsigned,unsigned,uint32_t);
uint32_t ps2_frontend_frame_cancel(Ps2FrontendFrame*,const Ps2FrontendFrameWrite*);
/* Copy the newest completed frame to private_memory+4096. This is called only
 * by the native SD supervisor's serialized FRAME, which owns its output buffer.
 * Successful return publishes width/height/sequence with a release fence. */
uint32_t ps2_frontend_frame_receive(Ps2FrontendFrame*,void*,size_t);
/* Stops new publications. The caller still performs all real joins/receipts.
 * Finish merely verifies that this shared queue has no borrowed pixel slot. */
uint32_t ps2_frontend_frame_stop(Ps2FrontendFrame*);
uint32_t ps2_frontend_frame_finish(Ps2FrontendFrame*);
#ifdef __cplusplus
}
#endif
#endif
