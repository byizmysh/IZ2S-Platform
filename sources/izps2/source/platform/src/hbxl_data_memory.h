#ifndef PS2_HBXL_DATA_MEMORY_H
#define PS2_HBXL_DATA_MEMORY_H
#include "heap_pages.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_HBXL_MAIN_DATA_BYTES ((size_t)298844160)
/* Main DataMemory only. Caller serializes all mutations, stops memory users
 * before permission changes/release, and keeps metadata resident. No pthread
 * or Linux object crosses an SDK call. Commit/decommit retain physical heap
 * ownership until release; executable memory and aliases are unsupported. */
uint32_t ps2_hbxl_data_initialize(void);
/* Explicit callback composition entry. The caller must independently verify
 * the live SDK callbacks and scalar ABI; sdk_base is evidence only. Used by
 * the external offline model and by an owner supplying qualified SDK wrappers.
 * This entry never sets sdk_fingerprints_verified. */
uint32_t ps2_hbxl_data_initialize_callbacks(const Ps2HeapPagesApi *,uintptr_t sdk_base);
uint32_t ps2_hbxl_data_shutdown(void);
Ps2SdkMemory *ps2_hbxl_data_manager(void);
uint32_t ps2_hbxl_data_allocate(Ps2MemoryRegion *,size_t,unsigned char *,size_t);
uint32_t ps2_hbxl_data_release(Ps2MemoryRegion *);
typedef struct {
    uintptr_t sdk_base;
    size_t live_bytes,committed_pages;
    uint32_t ready,quarantined,last_error,sdk_fingerprints_verified;
    uint32_t active_heap_blocks,active_regions;
    uint64_t allocations,releases,commits,decommits,protections;
} Ps2HbxlDataStatistics;
void ps2_hbxl_data_statistics(Ps2HbxlDataStatistics *);
#ifdef __cplusplus
}
#endif
#endif
