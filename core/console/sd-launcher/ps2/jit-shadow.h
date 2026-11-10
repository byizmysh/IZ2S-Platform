#ifndef IZ_PS2_JIT_SHADOW_H
#define IZ_PS2_JIT_SHADOW_H
#include "memory.h"
typedef struct {
 uint32_t memory_type,flags,heap,coherent;
 uint64_t heap_bytes;
} IzPs2ShadowSelection;
typedef struct {
 void*owner;
 uint32_t (*identity)(void*,uint64_t*);
 uint32_t (*query)(void*,uint64_t,IzPs2MemoryRegion*);
 uint32_t (*physical)(void*,uint64_t,uint32_t*,uint64_t*);
 uint32_t (*properties)(void*,uint64_t,uint8_t*);
 uint32_t (*allocate)(void*,uint64_t,const uint8_t*,uint64_t*);
 uint32_t (*map)(void*,uint64_t,uint64_t,uint64_t,uint64_t*);
 uint32_t (*unmap)(void*,uint64_t,uint64_t);
 uint32_t (*release)(void*,uint64_t,uint64_t);
} IzPs2ShadowOps;
enum {IZ_PS2_SHADOW_IDLE,IZ_PS2_SHADOW_PREPARING,IZ_PS2_SHADOW_READY,IZ_PS2_SHADOW_CLOSING,IZ_PS2_SHADOW_CLOSED,IZ_PS2_SHADOW_FAILED,IZ_PS2_SHADOW_RETAINED,IZ_PS2_SHADOW_ENTERED};
typedef struct {
 uint32_t busy,state,error,pending,phase,entered,map_known,regions;
 uint32_t protected_count,physical_count;
 uint64_t pid,instance,physical,device,memory,mapped,walk,end,units;
 uint64_t allocation_calls,map_calls,unmap_calls,free_calls;
 IzPs2ShadowOps ops;
 IzPs2ShadowSelection selection;
 IzPs2MemorySpan source,protected[10];
} IzPs2Shadow;
/* Pure original R176 memory-type selection and VkMemoryAllocateInfo packing.
 * Selection considers heap capacity, not currently free GPU memory. */
uint32_t iz_ps2_shadow_select(const uint8_t properties[520],IzPs2ShadowSelection*);
uint32_t iz_ps2_shadow_allocation(uint32_t memory_type,uint8_t out[32]);
/* The source is the already admitted SDK JIT source, not this cached shadow.
 * At most one provider call plus fresh process identity per step work unit.
 * The GPU provider must already have qualified its exact native bindings. */
uint32_t iz_ps2_shadow_begin(IzPs2Shadow*,const IzPs2ShadowOps*,const IzNativeEnvironment*,const IzNativeGraphics*,const IzPs2Memory*);
uint32_t iz_ps2_shadow_step(IzPs2Shadow*,uint32_t budget,uint32_t*done);
uint32_t iz_ps2_shadow_cancel(IzPs2Shadow*,uint32_t reason);
/* After entry intent, pre-entry cancellation cannot release GPU backing.
 * Actual PS2 VM/JIT nested joins and code-map receipts are still required. */
uint32_t iz_ps2_shadow_entered(IzPs2Shadow*);
#endif
