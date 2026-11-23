#ifndef PS2_SDK_MEMORY_H
#define PS2_SDK_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_MEMORY_PAGE 4096u
#define PS2_MEMORY_REGIONS 16u
/* Exact scalar ABI of nn::os VAM functions. SetMemoryPermission is void:
 * it returns normally on success and aborts within the SDK on failure.
 * Never reinterpret its residual return register as an nn::Result.
 * Binding does not initialize the process-wide VAM manager. */
typedef struct {
    bool (*is_enabled)(void);
    uint32_t (*reserve)(uintptr_t *,size_t);
    uint32_t (*commit)(uintptr_t,size_t);
    uint32_t (*decommit)(uintptr_t,size_t);
    uint32_t (*release)(uintptr_t);
    void (*protect)(uintptr_t,size_t,unsigned);
} Ps2SdkMemoryApi;
typedef struct Ps2SdkMemory Ps2SdkMemory;
typedef struct {
    Ps2SdkMemory *owner;
    uintptr_t base;
    size_t bytes,pages,committed;
    /* Caller-owned metadata, outside the reservation, remains live until release.
     * Each byte is 0 for uncommitted, or 0x80 | SDK permission (0,1,3).
     * Do not edit metadata or move a registered region. */
    unsigned char *states;
    uint32_t poisoned;
} Ps2MemoryRegion;
struct Ps2SdkMemory {
    Ps2SdkMemoryApi api;
    Ps2MemoryRegion *regions[PS2_MEMORY_REGIONS];
    uint32_t ready,poisoned,active,last_error;
    uint64_t calls,reservations,releases,commits,decommits,protections;
    size_t committed_pages;
};
enum { PS2_MEMORY_INVALID=0xf401, PS2_MEMORY_UNSUPPORTED=0xf402,
       PS2_MEMORY_BUSY=0xf403, PS2_MEMORY_BAD_REPLY=0xf404,
       PS2_MEMORY_POISONED=0xf405 };
/* All mutations are serialized by the owner. Exclude memory users during
 * release/decommit and arrange safe exception routing before protecting code
 * pages. This adapter owns data pages only, never executable mappings or aliases.
 * A failed commit/decommit/release poisons the region: physical ownership may
 * be uncertain, so later operations cannot silently recycle or free it. */
uint32_t ps2_sdk_memory_initialize(Ps2SdkMemory *,const Ps2SdkMemoryApi *);
uint32_t ps2_sdk_memory_reserve(Ps2SdkMemory *,size_t,unsigned char *,size_t,Ps2MemoryRegion *);
uint32_t ps2_sdk_memory_commit(Ps2MemoryRegion *,size_t,size_t);
uint32_t ps2_sdk_memory_decommit(Ps2MemoryRegion *,size_t,size_t);
uint32_t ps2_sdk_memory_protect(Ps2SdkMemory *,uintptr_t,size_t,unsigned);
uint32_t ps2_sdk_memory_release(Ps2MemoryRegion *);
#ifdef __cplusplus
}
#endif
#endif
