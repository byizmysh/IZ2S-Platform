#ifndef NATIVE_HEAVY_ABI_H
#define NATIVE_HEAVY_ABI_H
#include <stddef.h>
#include <stdint.h>
#define HEAVY_MAGIC 0x48565931u
typedef struct {
 uint32_t magic,version,status,error;
 uint32_t frames_target,frames_done,flags,phase;
 uint64_t wad,wad_bytes,heap,heap_bytes,stack,stack_bytes;
 uint64_t frame,frame_capacity,tick,tick_hz;
 uint64_t boot_start,boot_end,run_start,run_end;
 uint64_t image_hash,world_hash,heap_peak,trace,trace_capacity;
 volatile uint32_t stop;
 uint32_t script;
 uint64_t step_ticks,min_step_ticks,max_step_ticks;
 uint32_t changed_frames,last_frame;
 uint64_t display,error_text,error_capacity,reserved;
} HeavyContext;
typedef struct {uint32_t frame,buttons;uint64_t ticks,image_hash,world_hash;} HeavyTrace;
_Static_assert(sizeof(HeavyContext)==256,"native heavy context ABI");
_Static_assert(offsetof(HeavyContext,stack)==64,"entry stack ABI");
_Static_assert(offsetof(HeavyContext,stop)==184,"cooperative stop ABI");
_Static_assert(sizeof(HeavyTrace)==32,"trace ABI");
uint32_t heavy_main(HeavyContext *ctx);
#endif
