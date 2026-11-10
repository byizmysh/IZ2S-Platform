#ifndef IZ_PS2_MEMORY_H
#define IZ_PS2_MEMORY_H
#include "startup.h"
typedef struct {uint64_t base,bytes;uint32_t type,attributes,permission,ipc_refs,device_refs,padding;} IzPs2MemoryRegion;
_Static_assert(sizeof(IzPs2MemoryRegion)==40,"Self QueryMemory ABI");
typedef struct {
 void*owner;
 uint32_t (*identity)(void*,uint64_t*);
 uint32_t (*allocate)(void*,uint64_t,uint64_t*);
 uint32_t (*query)(void*,uint64_t,IzPs2MemoryRegion*);
 uint32_t (*release)(void*,uint64_t);
} IzPs2MemoryOps;
enum {IZ_PS2_MAIN,IZ_PS2_JIT_SOURCE,IZ_PS2_ENTRY_STACK,IZ_PS2_DISPATCH,IZ_PS2_HWGS_LEDGER,IZ_PS2_MEMORY_BLOCKS};
enum {IZ_PS2_MEMORY_IDLE,IZ_PS2_MEMORY_PREPARING,IZ_PS2_MEMORY_READY,IZ_PS2_MEMORY_CLOSING,IZ_PS2_MEMORY_CLOSED,IZ_PS2_MEMORY_FAILED,IZ_PS2_MEMORY_RETAINED,IZ_PS2_MEMORY_ENTERED};
typedef struct {uint64_t base,bytes;} IzPs2MemorySpan;
typedef struct {
 uint32_t busy,state,error,pending,phase,count,wanted,hwgs;
 uint32_t index,walk_regions,entered,protected_count;
 uint64_t pid,walk_at,walk_end,units,allocation_calls,free_calls;
 IzPs2MemoryOps ops;
 IzPs2MemorySpan blocks[IZ_PS2_MEMORY_BLOCKS],protected[4];
} IzPs2Memory;

/* Begin and after_image only establish plans. Each step performs at most one
 * allocation, query or free plus a fresh self identity check. Main is ready
 * first; image loading occurs before after_image admits the other blocks.
 * None of these methods enters PS2 or touches GPU state. */
uint32_t iz_ps2_memory_begin(IzPs2Memory*,const IzPs2MemoryOps*,const IzNativeEnvironment*,uint64_t parent,uint64_t parent_bytes,uint32_t hwgs);
uint32_t iz_ps2_memory_after_image(IzPs2Memory*,const IzNativeImage*);
uint32_t iz_ps2_memory_step(IzPs2Memory*,uint32_t budget,uint32_t*done);
uint32_t iz_ps2_memory_cancel(IzPs2Memory*,uint32_t reason);
/* Entry changes release authority. Post-entry release additionally needs the
 * real PS2 VM/Main/AV/storage/JIT receipts, not just the outer worker's join.
 * This preparation API deliberately supplies no post-entry free shortcut. */
uint32_t iz_ps2_memory_entered(IzPs2Memory*);
#endif
