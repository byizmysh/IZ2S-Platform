#include "host-services.h"
#include "nvkmd_nvgpu_native_views.h"
#include <stdio.h>
#include <string.h>

static struct nvkmd_nvgpu_native_runtime runtime;
static struct nvkmd_nvgpu_native_backing backing;
static struct nvkmd_nvgpu_native_device_owner owner;
static Result map_result, unmap_result, waits[8];
static unsigned wait_count, wait_at, maps, unmaps, frees, reclaims;
static unsigned reclaim_expected;

bool nvkmd_nvgpu_native_runtime_ready(struct nvkmd_nvgpu_native_runtime *r) { return r == &runtime; }
bool nvkmd_nvgpu_native_owner_lock(struct nvkmd_nvgpu_native_device_owner *o) { return o == &owner; }
void nvkmd_nvgpu_native_owner_unlock(struct nvkmd_nvgpu_native_device_owner *o) { assert(o == &owner); }
Result eventWait(Event *e, uint64_t timeout) {
    (void)e; assert(timeout == UINT64_C(1000000000));
    return wait_at < wait_count ? waits[wait_at++] : 0;
}
void eventClose(Event *e) { (void)e; }
Result gsvGpuCreateSlotEvent(GsvGpu *g, uint32_t index, Event *e, bool autoclear) {
    (void)g; (void)index; (void)e; (void)autoclear; return 0;
}
Result gsvGpuMap(GsvGpu *g, const GsvGpuMapping *m, uint32_t index, u8 *kind) {
    (void)g; (void)index; maps++; *kind = m->kind; return map_result;
}
Result gsvGpuUnmap(GsvGpu *g, uint32_t vm, uint64_t va, uint64_t size, uint32_t index) {
    (void)g; (void)vm; (void)va; (void)size; (void)index; unmaps++; return unmap_result;
}
Result gsvGpuChangeKind(GsvGpu *g, uint32_t vm, uint64_t va, uint64_t size, uint8_t kind, uint32_t index, u8 *selected) {
    (void)g; (void)vm; (void)va; (void)size; (void)index; *selected = kind; return 0;
}
void nvkmd_nvgpu_native_owner_release_va(struct nvkmd_nvgpu_native_device_owner *o, uint64_t va, uint64_t size) {
    assert(o == &owner && va == 0x10000 && size == 0x4000 && backing.maps == 0); frees++;
}
void nvkmd_nvgpu_native_owner_reclaim_chunks(struct nvkmd_nvgpu_native_device_owner *o) {
    assert(o == &owner && backing.maps == reclaim_expected); reclaims++;
}
static void reset(void) {
    memset(&runtime, 0, sizeof runtime); runtime.generation = 7;
    memset(&backing, 0, sizeof backing); backing.self = &backing;
    backing.ready = backing.registered = true; backing.generation = 7; backing.length = 0x4000;
    memset(&owner, 0, sizeof owner); owner.runtime = &runtime; owner.slot.ready = true;
    map_result = unmap_result = 0; wait_count = wait_at = maps = unmaps = frees = reclaims = reclaim_expected = 0;
}
static void finish(void) {
    assert(backing.maps == 0 && owner.retired_view_lists == NULL);
    assert(nvkmd_nvgpu_native_slot_close(&owner.slot));
}
static enum nvkmd_nvgpu_native_map_status map_range(void) {
    Result result;
    return nvkmd_nvgpu_native_map_range(&runtime, &backing, 0x10000, 0, 0x4000, 6, true, &owner.slot, &result);
}
static enum nvkmd_nvgpu_native_map_status unmap_range(void) {
    Result result;
    return nvkmd_nvgpu_native_unmap_range(&runtime, &backing, 0x10000, 0x4000, &owner.slot, &result);
}
static struct nvkmd_nvgpu_native_view_list *new_view(void) {
    struct nvkmd_nvgpu_native_view_list *list = nvkmd_nvgpu_native_view_list_create(0x4000, 6);
    assert(list); list->va_addr = 0x10000;
    struct nvkmd_nvgpu_native_view view = {.size_B = 0x4000, .source = {.backing = &backing}};
    assert(nvkmd_nvgpu_native_view_list_map(&owner, list, &view) == VK_SUCCESS);
    return list;
}
int main(void) {
    reset(); assert(map_range() == NVKMD_NVGPU_NATIVE_MAP_OK && backing.maps == 1);
    assert(unmap_range() == NVKMD_NVGPU_NATIVE_MAP_OK && backing.maps == 0); finish();
    puts("PASS completed map and unmap release exactly once");

    reset(); waits[0] = waits[1] = KERNELRESULT(TimedOut); waits[2] = 0; wait_count = 3;
    assert(map_range() == NVKMD_NVGPU_NATIVE_MAP_OK && wait_at == 3 && maps == 1);
    assert(unmap_range() == NVKMD_NVGPU_NATIVE_MAP_OK); finish();
    puts("PASS slow request waits through timeouts without resubmission");

    reset(); map_result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    assert(map_range() == NVKMD_NVGPU_NATIVE_MAP_REPLY && backing.maps == 0);
    assert(owner.slot.pending.request == NVKMD_NVGPU_NATIVE_SLOT_IDLE); finish();
    puts("PASS refused request leaves no mapping");

    reset(); map_result = KERNELRESULT(InvalidHandle);
    assert(map_range() == NVKMD_NVGPU_NATIVE_MAP_WAIT && backing.maps == 1);
    assert(owner.slot.pending.acceptance_unknown && !nvkmd_nvgpu_native_slot_close(&owner.slot));
    assert(unmap_range() == NVKMD_NVGPU_NATIVE_MAP_SLOT && unmaps == 0);
    /* End the mocked process: no real allocation/service was created. */
    memset(&owner.slot.pending, 0, sizeof owner.slot.pending); backing.maps = 0; finish();
    puts("PASS unknown transport result preserves ownership and blocks slot reuse");

    reset(); assert(map_range() == NVKMD_NVGPU_NATIVE_MAP_OK);
    waits[0] = KERNELRESULT(InvalidHandle); wait_count = 1;
    assert(unmap_range() == NVKMD_NVGPU_NATIVE_MAP_WAIT && backing.maps == 1);
    assert(unmap_range() == NVKMD_NVGPU_NATIVE_MAP_OK && unmaps == 1 && backing.maps == 0); finish();
    puts("PASS delayed unmap completion does not double-unmap");

    reset(); struct nvkmd_nvgpu_native_view_list *list = new_view();
    unmap_result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    assert(nvkmd_nvgpu_native_view_list_unbind(&owner, list, 0x1000, 0x1000) == VK_ERROR_DEVICE_LOST);
    assert(list->count == 1 && list->views[0].failed && backing.maps == 1 && maps == 1);
    unmap_result = 0;
    assert(nvkmd_nvgpu_native_view_list_unbind(&owner, list, 0x1000, 0x1000) == VK_SUCCESS);
    assert(list->count == 0 && backing.maps == 0 && maps == 1);
    nvkmd_nvgpu_native_view_list_finish(&owner, list); assert(frees == 1); finish();
    puts("PASS failed view is retired whole without remapping its fragments");

    reset(); list = new_view(); owner.stranded_chunks = &backing; reclaim_expected = 2;
    assert(nvkmd_nvgpu_native_view_list_unbind(&owner, list, 0x1000, 0x1000) == VK_SUCCESS);
    assert(list->count == 2 && backing.maps == 2 && maps == 3 && reclaims == 1);
    assert(list->views[0].offset_B == 0 && list->views[0].size_B == 0x1000);
    assert(list->views[1].offset_B == 0x2000 && list->views[1].size_B == 0x2000);
    reclaim_expected = 0; nvkmd_nvgpu_native_view_list_finish(&owner, list); assert(frees == 1); finish();
    puts("PASS partial unbind keeps chunks until surviving ranges are remapped");

    reset(); list = new_view(); unmap_result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    nvkmd_nvgpu_native_view_list_finish(&owner, list);
    assert(owner.retired_view_lists == list && frees == 0 && backing.maps == 1);
    assert(!nvkmd_nvgpu_native_owner_release_retired_views(&owner) && frees == 0);
    unmap_result = 0; assert(nvkmd_nvgpu_native_owner_release_retired_views(&owner));
    assert(frees == 1 && backing.maps == 0); finish();
    puts("PASS failed destruction retains VA until a successful retry");
}
