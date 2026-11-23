#ifndef IZ_PS2_VIDEO_H
#define IZ_PS2_VIDEO_H
#include "av.h"
#include "memory.h"
enum {IZ_PS2_VIDEO_CREATE_SEMAPHORE=IZ_GFX_COUNT,IZ_PS2_VIDEO_DESTROY_SEMAPHORE,IZ_PS2_VIDEO_FUNCTIONS};
typedef struct {
 void*owner;
 uint32_t (*identity)(void*,uint64_t*);
 uint32_t (*query)(void*,uint64_t,IzPs2MemoryRegion*);
 uint32_t (*call)(void*,uint32_t,const uint64_t args[8],uint64_t*result);
} IzPs2VideoOps;
typedef struct {
 const void*vertex,*fragment;uint32_t vertex_bytes,fragment_bytes;
 uint32_t protected_count,reserved;
 IzPs2AvSpan protected[24];
} IzPs2VideoPlan;
enum {IZ_PS2_VIDEO_IDLE,IZ_PS2_VIDEO_PREPARING,IZ_PS2_VIDEO_READY,IZ_PS2_VIDEO_CLOSING,IZ_PS2_VIDEO_CLOSED,IZ_PS2_VIDEO_FAILED,IZ_PS2_VIDEO_RETAINED,IZ_PS2_VIDEO_ENTERED};
typedef struct {
 IzPs2VideoSource source;
 uint64_t shaders[2],pipeline,requirements[3];
 uint32_t memory_type,mapped_known;
} IzPs2VideoSlot;
typedef struct {
 uint32_t busy,state,error,pending,phase,slot,index,record_step;
 uint32_t entered,regions,closing_stage,reserved;
 uint64_t pid,walk,end,cleared,units,calls,queries,queue_waits;
 IzPs2VideoOps ops;IzNativeGraphics graphics;IzPs2VideoPlan plan;
 IzPs2VideoSlot slots[2];
} IzPs2Video;
/* The owner/ops/shader data live in the controller's retained allocation.
 * Begin performs no provider call. Each step unit makes at most one Vulkan
 * call, mapping query or 64 KiB clear, plus a fresh process identity check.
 * Renderer objects are borrowed. No acquire, submit, present or engine entry
 * occurs during preparation. The qualified original fragment uses binding1;
 * the original compute shader has no operations and is not instantiated. */
uint32_t iz_ps2_video_begin(IzPs2Video*,const IzPs2VideoOps*,const IzNativeEnvironment*,const IzNativeGraphics*,const IzPs2VideoPlan*);
uint32_t iz_ps2_video_step(IzPs2Video*,uint32_t budget,uint32_t*done);
uint32_t iz_ps2_video_cancel(IzPs2Video*,uint32_t reason);
uint32_t iz_ps2_video_pack_av(IzPs2Video*,const IzPs2AvInput*,uint8_t out[4096]);
/* Set immediately before actual engine entry. Even a joined outer worker
 * cannot authorize cleanup: actual PS2 nested worker/AV receipts are needed.
 * There is intentionally no post-entry release shortcut in this factory. */
uint32_t iz_ps2_video_entered(IzPs2Video*);
#endif
