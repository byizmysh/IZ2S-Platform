#ifndef IZ_RELOCATABLE_LOADER_H
#define IZ_RELOCATABLE_LOADER_H
#include "image.h"
#include "../native/abi.h"
#include "address.h"
enum {IZ_IMAGE_HEADER=1,IZ_IMAGE_CODE,IZ_IMAGE_DATA,IZ_IMAGE_FIXUPS,IZ_IMAGE_MAP,IZ_IMAGE_READY,IZ_IMAGE_CLOSED,IZ_IMAGE_FAILED,IZ_IMAGE_RETAINED,IZ_IMAGE_CLOSING};
/* NULL selects a fresh self-process window without a PC callback. A supplied
 * owner plan is independently admitted against current reserved/free ranges.
 * Both paths re-admit after reading/fixing the image, before executable maps. */
typedef IzAddressPlan IzImagePlan;
typedef struct {
 u32 busy,pending,stage,error,ready,records_processed;
 IzFs *fs;IzRelocHeader header;
 u64 source,data,destination,pages,mapped,entry,spawn,read_bytes,program_id;
 u32 file,identity_bound;
 u32 operation,phase,section_crc,has_plan,verified;
 u64 file_bytes,offset,walk_at,walk_end,units,cleared_bytes;
 u32 walk_regions,walk_type,walk_code,walk_next,last_units;
 IzImagePlan plan;IzAddressCursor address;IzRelocState reloc;IzRelocReach reach;
 char path[769];
} IzImage;
typedef u32 (*IzImageProgress)(void*,const IzImage*);
/* Serialized console-owner operation. Uses the borrowed filesystem/RO
 * capabilities and already verified allocator. It never calls engine code.
 * Source code, data and fixups are read through at most 64 KiB of IPC scratch.
 * Executable mappings start only after every checksum and fixup succeeds. */
u32 iz_image_load(IzFs*,IzImage*,const char*,const IzImagePlan*,IzImageProgress,void*);
/* Caller must not have entered the engine, or must have joined all engine
 * threads/users and validated their real cleanup receipts before unloading. */
u32 iz_image_unload(IzImage*);
/* Cooperative core. Begin/close do no filesystem or RO work. Each unit of
 * step performs at most one 64 KiB read/clear/fixup batch, one RO request,
 * one allocator action or one address-cursor action. Budget is 1..64 units.
 * A known error completes bounded cleanup before done=1 returns that error.
 * Unknown outcomes become retained immediately and admit no more SDK work. */
u32 iz_image_begin(IzFs*,IzImage*,const char*,const IzImagePlan*);
u32 iz_image_step(IzImage*,u32 budget,u32*done);
u32 iz_image_close_begin(IzImage*,u32 error);
#endif
