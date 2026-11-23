#ifndef IZ_PS2_SDK_MEMORY_H
#define IZ_PS2_SDK_MEMORY_H
#include "memory.h"
typedef struct {uint64_t sdk;IzPs2MemoryOps ops;} IzPs2SdkMemory;
/* Read-only local qualification against current process identity, RX mapping
 * and exact existing SDK bytecuts. No host verification request is involved.
 * Keep this borrowed SDK binding alive until every memory owner is resolved. */
uint32_t iz_ps2_sdk_memory_bind(IzPs2SdkMemory*,const IzNativeEnvironment*);
uint32_t iz_ps2_sdk_check_cut(uint64_t sdk,uint64_t offset,const uint8_t*,uint64_t bytes);
#endif
