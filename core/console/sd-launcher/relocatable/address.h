#ifndef IZ_SD_ADDRESS_H
#define IZ_SD_ADDRESS_H
#include "../native/abi.h"
typedef struct {u64 code_base,window_base,window_bytes;} IzAddressPlan;
typedef struct {u64 base,bytes;} IzAddressRange;
typedef struct {u64 pid;IzAddressRange aslr,reserved[3];} IzAddressSpace;
typedef struct {
 IzAddressSpace space;IzAddressPlan plan;
 u64 at,end,bytes;u32 mode,regions,error,done;
} IzAddressCursor;
typedef struct {u64 base,bytes;u32 type,attributes,permission,ipc_refs,device_refs,padding;} IzAddressRegion;
_Static_assert(sizeof(IzAddressRegion)==40,"QueryMemory MemoryInfo ABI");
enum {IZ_ADDRESS_INVALID=0xe620,IZ_ADDRESS_FULL=0xe621,IZ_ADDRESS_LIMIT=0xe622,IZ_ADDRESS_PID=0xe623};
/* Read-only self-process SVC boundaries, derived from the bundled RO wrappers.
 * Host tests implement exactly these three kernel boundaries, not the planner. */
u32 iz_self_process_id(u64*);
u32 iz_self_info(u64*,u32);
u32 iz_self_query(IzAddressRegion*,u64);
/* Every call captures current self identity and ASLR/alias/heap/stack ranges.
 * No cached session addresses or per-engine host request is involved. Plans
 * do not reserve memory: the owner must serialize mappings and handle races. */
u32 iz_address_find(IzContext*,u64,IzAddressPlan*);
u32 iz_address_admit(IzContext*,const IzAddressPlan*,u64);
/* Capture is a fixed ten read-only SVC calls. Each step queries at most one
 * memory region, so a fragmented address space cannot monopolize a frame. */
u32 iz_address_begin_bounded(IzContext*,IzAddressCursor*,u64,const IzAddressPlan*,const IzAddressRange*);
u32 iz_address_begin(IzContext*,IzAddressCursor*,u64,const IzAddressPlan*);
u32 iz_address_step(IzContext*,IzAddressCursor*,u32*done);
#endif
