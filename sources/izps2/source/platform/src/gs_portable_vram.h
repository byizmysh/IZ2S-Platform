#ifndef PS2_GS_PORTABLE_VRAM_H
#define PS2_GS_PORTABLE_VRAM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t allocations, frees, live_objects, live_bytes;
    uint64_t peak_bytes, failures, allocation_bytes, release_bytes;
} Ps2GsPortableStatistics;
extern Ps2GsPortableStatistics ps2_gs_portable_stats;
void *__wrap__Z23GSAllocateWrappedMemorymm(size_t size, size_t repeat);
void __wrap__Z19GSFreeWrappedMemoryPvmm(void *pointer, size_t size, size_t repeat);
#ifdef __cplusplus
}
#endif
#endif
