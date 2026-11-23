#include "hbxl_data_memory.h"
#include "memory_bridge.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include "hbxl_data_sdk_fingerprints.h"
#include <string.h>
static Ps2HeapPages heap;
static Ps2SdkMemory memory;
static uintptr_t sdk_base;
static uint32_t ready,last_error,fingerprints;
static uint32_t failed(uint32_t result){last_error=result;return result;}
static int quarantined(void){
    if(heap.poisoned || memory.poisoned)return 1;
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++)if(memory.regions[i] && memory.regions[i]->poisoned)return 1;
    return 0;
}
__attribute__((noreturn)) static void fatal(uint32_t error){
    last_error=error;heap.poisoned=1;
    /* Unknown native ownership must survive. Never return through HBXL's
     * process-exit cleanup while an uncertain SDK allocation is retained. */
    for(;;)hbxl_svc_sleep(10000000);
}
uint32_t ps2_hbxl_data_initialize_callbacks(const Ps2HeapPagesApi *api,uintptr_t base){
    if(ready || heap.ready || heap.active || memory.ready || memory.active || quarantined() || ps2_armsx2_memory_bound())return failed(PS2_MEMORY_BUSY);
    uint32_t result=ps2_heap_pages_bind(&heap,api,PS2_HBXL_MAIN_DATA_BYTES);
    if(result)return failed(result);
    result=ps2_sdk_memory_initialize(&memory,ps2_heap_pages_memory_api());
    if(!result)result=ps2_armsx2_bind_memory(&memory);
    if(result){
        /* Binding has not allocated a block. Only a clean provider can undo
         * this setup; preserve any unexpected ownership instead of resetting. */
        if(heap.active || heap.live_bytes || memory.active || quarantined())return failed(PS2_MEMORY_POISONED);
        uint32_t cleanup=ps2_heap_pages_unbind(&heap);if(cleanup)return failed(cleanup);
        memset(&memory,0,sizeof(memory));return failed(result);
    }
    sdk_base=base;fingerprints=0;ready=1;last_error=0;return 0;
}
uint32_t ps2_hbxl_data_initialize(void){
    if(ready || heap.ready || memory.ready || quarantined())return failed(PS2_MEMORY_BUSY);
    const struct hbx_ctx *ctx=hbxl_ctx;
    if(!ctx || ctx->magic!=HBX_ABI_MAGIC || ctx->abi_version!=HBX_ABI_VERSION || ctx->size<sizeof(*ctx) || ctx->audio.initialize_parameter<PS2_HBXL_AUDIO_INIT_OFFSET)return failed(PS2_MEMORY_UNSUPPORTED);
    uintptr_t base=(uintptr_t)(ctx->audio.initialize_parameter-PS2_HBXL_AUDIO_INIT_OFFSET);
    if(!base || (base&(PS2_MEMORY_PAGE-1)) || base>UINTPTR_MAX-PS2_HBXL_MEMALIGN_OFFSET-256 ||
       hbxl_imp[IMP_MEMALIGN]!=base+PS2_HBXL_MEMALIGN_OFFSET || hbxl_imp[IMP_FREE]!=base+PS2_HBXL_DELETE_OFFSET)return failed(PS2_MEMORY_UNSUPPORTED);
    /* Same byte cuts checked before the successful native-memory hardware
     * qualification. Presence in a symbol catalogue alone is insufficient. */
    if(memcmp((const void *)(base+PS2_HBXL_MEMALIGN_OFFSET),ps2_hbxl_memalign_bytes,sizeof(ps2_hbxl_memalign_bytes)) ||
       memcmp((const void *)(base+PS2_HBXL_DELETE_OFFSET),ps2_hbxl_delete_bytes,sizeof(ps2_hbxl_delete_bytes)) ||
       memcmp((const void *)(base+PS2_HBXL_PERMISSION_OFFSET),ps2_hbxl_permission_bytes,sizeof(ps2_hbxl_permission_bytes)))return failed(PS2_MEMORY_UNSUPPORTED);
    Ps2HeapPagesApi api={
        (int(*)(void **,size_t,size_t))(base+PS2_HBXL_MEMALIGN_OFFSET),
        (void(*)(void *))(base+PS2_HBXL_DELETE_OFFSET),
        (void(*)(uintptr_t,size_t,unsigned))(base+PS2_HBXL_PERMISSION_OFFSET),fatal};
    uint32_t result=ps2_hbxl_data_initialize_callbacks(&api,base);
    if(!result)fingerprints=1;
    return result;
}
Ps2SdkMemory *ps2_hbxl_data_manager(void){return ready?&memory:0;}
uint32_t ps2_hbxl_data_allocate(Ps2MemoryRegion *region,size_t bytes,unsigned char *states,size_t capacity){
    if(!ready)return failed(PS2_MEMORY_UNSUPPORTED);
    if(quarantined())return failed(PS2_MEMORY_POISONED);
    uint32_t result=ps2_sdk_memory_reserve(&memory,bytes,states,capacity,region);
    if(!result)result=ps2_sdk_memory_commit(region,0,bytes);
    return result?failed(result):0;
}
uint32_t ps2_hbxl_data_release(Ps2MemoryRegion *region){
    if(!ready || !region || region->owner!=&memory)return failed(PS2_MEMORY_INVALID);
    if(quarantined() || region->poisoned)return failed(PS2_MEMORY_POISONED);
    /* All pages must still be committed. Owners using partial commit/decommit
     * directly must restore a coherent region or use sdk_memory release. */
    uint32_t result=ps2_sdk_memory_decommit(region,0,region->bytes);
    if(!result)result=ps2_sdk_memory_release(region);
    return result?failed(result):0;
}
uint32_t ps2_hbxl_data_shutdown(void){
    if(!ready)return failed(PS2_MEMORY_INVALID);
    if(quarantined())return failed(PS2_MEMORY_POISONED);
    if(heap.active || heap.live_bytes || memory.active || memory.committed_pages)return failed(PS2_MEMORY_BUSY);
    uint32_t result=ps2_armsx2_bind_memory(0);if(result)return failed(result);
    result=ps2_heap_pages_unbind(&heap);if(result)return failed(result);
    memset(&memory,0,sizeof(memory));memset(&heap,0,sizeof(heap));sdk_base=0;fingerprints=0;ready=0;last_error=0;return 0;
}
void ps2_hbxl_data_statistics(Ps2HbxlDataStatistics *out){
    if(!out)return;
    *out=(Ps2HbxlDataStatistics){.sdk_base=sdk_base,.live_bytes=heap.live_bytes,.committed_pages=memory.committed_pages,
        .ready=ready,.quarantined=quarantined(),.last_error=last_error,.sdk_fingerprints_verified=fingerprints,
        .active_heap_blocks=heap.active,.active_regions=memory.active,.allocations=heap.allocations,.releases=heap.releases,
        .commits=heap.commits,.decommits=heap.decommits,.protections=heap.protections};
}
