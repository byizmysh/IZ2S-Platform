#ifndef PS2_GS_SHARED_VRAM_H
#define PS2_GS_SHARED_VRAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t base, bytes;
    uint32_t type, attributes, permission, device_refs, ipc_refs, reserved;
} Ps2GsMemoryInfo;
typedef struct {
    uint32_t (*create)(uint32_t*,uint64_t,uint32_t,uint32_t);
    uint32_t (*map)(uint32_t,uintptr_t,uint64_t,uint32_t);
    uint32_t (*unmap)(uint32_t,uintptr_t,uint64_t);
    uint32_t (*close)(uint32_t);
    uint32_t (*query)(Ps2GsMemoryInfo*,uint32_t*,uintptr_t);
    uint32_t (*info)(uint64_t*,uint32_t,uint32_t,uint64_t);
} Ps2GsMirrorApi;
typedef struct {
    uint64_t creates,closes,maps,unmaps,allocations,frees,live_objects,live_mappings;
    uint32_t last_error,poisoned,busy,configured;
} Ps2GsMirrorStatistics;
extern Ps2GsMirrorStatistics ps2_gs_mirror_stats;
enum { PS2_GS_MIRROR_INVALID=0xf831,PS2_GS_MIRROR_BUSY=0xf832,
       PS2_GS_MIRROR_RANGE=0xf833,PS2_GS_MIRROR_SDK=0xf834 };
/* A successful map must alias the same backing object, not make a copy.
 * Failed kernel operations are atomic. Cleanup failures retain the slot and
 * poison this allocator; no retry may forget the outstanding handle/views. */
uint32_t ps2_gs_mirror_bind(const Ps2GsMirrorApi*);
void *ps2_gs_mirror_allocate(size_t,size_t);
uint32_t ps2_gs_mirror_release(void*,size_t,size_t);
uint32_t ps2_gs_mirror_initialize_sdk(uintptr_t,size_t);
void *__wrap__Z23GSAllocateWrappedMemorymm(size_t,size_t);
void __wrap__Z19GSFreeWrappedMemoryPvmm(void*,size_t,size_t);
#ifdef __cplusplus
}
#endif
#endif
