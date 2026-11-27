/* Host service doubles only. The tested map/view code is compiled unchanged. */
#ifndef IZ2S_HOST_SERVICES_H
#define IZ2S_HOST_SERVICES_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <switch/result.h>
#include <vulkan/vulkan_core.h>

#define NVKMD_NVGPU_NATIVE_RUNTIME_H
#define NVKMD_NVGPU_NATIVE_DEVICE_OWNER_H
#define NVKMD_NVGPU_NATIVE_CHUNKED_H
typedef struct { unsigned id; } Event;
typedef struct { unsigned id; } GsvGpu;
typedef struct { unsigned unused; } MemoryInfo;
typedef struct {
    uint32_t vm_id, range_id;
    uint64_t va, size, range_offset, client;
    uint8_t kind, fallback_kind;
    bool gpu_cacheable;
} GsvGpuMapping;
struct nvkmd_nvgpu_native_runtime {
    uint64_t generation;
    struct { uint32_t lower, upper, slot; } worker;
    struct { uint32_t id; uint64_t client; } vm;
    struct { uint32_t fd; } as;
    GsvGpu gpu;
};
bool nvkmd_nvgpu_native_runtime_ready(struct nvkmd_nvgpu_native_runtime *);
Result eventWait(Event *, uint64_t);
void eventClose(Event *);
Result gsvGpuCreateSlotEvent(GsvGpu *, uint32_t, Event *, bool);
Result gsvGpuMap(GsvGpu *, const GsvGpuMapping *, uint32_t, u8 *);
Result gsvGpuUnmap(GsvGpu *, uint32_t, uint64_t, uint64_t, uint32_t);
Result gsvGpuChangeKind(GsvGpu *, uint32_t, uint64_t, uint64_t, uint8_t, uint32_t, u8 *);

#include "nvkmd_nvgpu_native_map.h"
#include "nvkmd_nvgpu_native_backing.h"
struct nvkmd_nvgpu_native_view_list;
struct nvkmd_nvgpu_native_device_owner {
    struct nvkmd_nvgpu_native_runtime *runtime;
    struct nvkmd_nvgpu_native_slot slot;
    struct { uint64_t view_maps, view_map_failures; } stats;
    void *stranded_chunks;
    struct nvkmd_nvgpu_native_view_list *retired_view_lists;
};
struct nvkmd_nvgpu_native_view_source {
    struct nvkmd_nvgpu_native_backing *backing;
    uint64_t backing_offset_B;
    bool gpu_cacheable;
};
bool nvkmd_nvgpu_native_owner_lock(struct nvkmd_nvgpu_native_device_owner *);
void nvkmd_nvgpu_native_owner_unlock(struct nvkmd_nvgpu_native_device_owner *);
void nvkmd_nvgpu_native_owner_reclaim_chunks(struct nvkmd_nvgpu_native_device_owner *);
void nvkmd_nvgpu_native_owner_release_va(struct nvkmd_nvgpu_native_device_owner *, uint64_t, uint64_t);
#endif
