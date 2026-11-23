#include "hbxl_data_memory_preallocated.h"
#include "memory_bridge.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include "hbxl_data_sdk_fingerprints.h"
#include <string.h>
#include <stddef.h>

_Static_assert(sizeof(Ps2HbxlPreallocatedMain)==136,"preallocated Main descriptor");
_Static_assert(sizeof(Ps2HbxlPreallocatedReceipt)==112,"preallocated Main receipt");
_Static_assert(offsetof(Ps2HbxlPreallocatedMain,descriptor_self)==128,"descriptor self");
_Static_assert(offsetof(Ps2HbxlPreallocatedReceipt,claims)==88,"receipt claims");
_Static_assert(offsetof(Ps2HbxlPreallocatedReceipt,last_error)==104,"receipt error");
typedef struct { uintptr_t base; size_t bytes; } Range;
static struct {
    Ps2HeapPages heap;
    Ps2SdkMemory memory;
    Ps2HeapPagesApi backing;
    Ps2HbxlPreallocatedMain saved;
    const Ps2HbxlPreallocatedMain *descriptor;
    Ps2HbxlPreallocatedReceipt *receipt;
    uintptr_t sdk_base;
    uint32_t ready,last_error,fingerprints,ever_bound,claimed,free_started;
} owner;
enum {PROTECT_IN_FLIGHT=0xf711,FREE_IN_FLIGHT=0xf712};

static uint32_t failed(uint32_t result){owner.last_error=result;return result;}
static int quarantined(void){
    if(owner.heap.poisoned || owner.memory.poisoned)return 1;
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++)
        if(owner.memory.regions[i] && owner.memory.regions[i]->poisoned)return 1;
    return 0;
}
static int range(uint64_t base,uint64_t bytes,Range *out){
    const uint64_t limit=UINT64_C(1)<<39;
    if(!base || !bytes || base>=limit || bytes>limit-base)return 0;
    *out=(Range){(uintptr_t)base,(size_t)bytes};return 1;
}
static int pages(uint64_t base,uint64_t bytes,Range *out){
    if((base&4095) || bytes>UINT64_MAX-4095)return 0;
    return range(base,(bytes+4095)&~UINT64_C(4095),out);
}
static int overlaps(Range a,Range b){
    return a.base<=b.base ? b.base-a.base<a.bytes : a.base-b.base<b.bytes;
}
static uint32_t receipt_state(void){return __atomic_load_n(&owner.receipt->state,__ATOMIC_ACQUIRE);}
static void state(uint32_t value){__atomic_store_n(&owner.receipt->state,value,__ATOMIC_RELEASE);}
static int immutable(void){return !memcmp(owner.descriptor,&owner.saved,sizeof(owner.saved));}
static int receipt_identity(const Ps2HbxlPreallocatedReceipt *r,const Ps2HbxlPreallocatedMain *d){
    return r->magic==PS2_PREALLOC_RECEIPT_MAGIC && r->version==1 && r->size==112 &&
        r->descriptor==d->descriptor_self && r->base==d->base && r->bytes==d->bytes &&
        r->nonce[0]==d->nonce[0] && r->nonce[1]==d->nonce[1] &&
        r->context==d->context && r->code_base==d->code_base &&
        r->data_base==d->data_base && r->sdk_base==d->sdk_base && !r->reserved;
}
static int live(void){
    return owner.claimed && !owner.free_started && immutable() &&
        receipt_identity(owner.receipt,&owner.saved) && receipt_state()==PS2_PREALLOC_CLAIMED &&
        owner.receipt->claims==1 && owner.receipt->free_calls==0 && !owner.receipt->last_error;
}
__attribute__((noreturn)) static void native_fatal(uint32_t error){
    (void)error;for(;;)hbxl_svc_sleep(10000000);
}
__attribute__((noreturn)) static void fatal(uint32_t error){
    owner.last_error=error;owner.heap.poisoned=1;
    /* Only this previously validated caller-owned receipt is touched. A changed
     * descriptor cannot redirect writes, permissions, or free to another block. */
    if(owner.receipt && owner.claimed){owner.receipt->last_error=error;state(PS2_PREALLOC_RETAINED);}
    if(owner.backing.fatal)owner.backing.fatal(error);
    native_fatal(error);
}
static int claim(void **out,size_t alignment,size_t bytes){
    if(!out)return PS2_MEMORY_INVALID;
    *out=0;
    if(alignment!=4096 || bytes!=PS2_HBXL_MAIN_DATA_BYTES || owner.claimed || owner.free_started ||
       !immutable() || !receipt_identity(owner.receipt,&owner.saved) ||
       owner.receipt->claims || owner.receipt->free_calls || owner.receipt->last_error)return PS2_MEMORY_INVALID;
    uint32_t expected=PS2_PREALLOC_PREPARED;
    if(!__atomic_compare_exchange_n(&owner.receipt->state,&expected,PS2_PREALLOC_CLAIMED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return PS2_MEMORY_BUSY;
    owner.claimed=1;owner.receipt->claims=1;*out=(void*)(uintptr_t)owner.saved.base;return 0;
}
static void protect(uintptr_t address,size_t bytes,unsigned permission){
    if(!live() || (address&4095) || !bytes || (bytes&4095) ||
       address<owner.saved.base || address-owner.saved.base>=owner.saved.bytes ||
       bytes>owner.saved.bytes-(address-owner.saved.base) ||
       (permission!=0 && permission!=1 && permission!=3))fatal(PS2_MEMORY_POISONED);
    owner.receipt->last_error=PROTECT_IN_FLIGHT;state(PS2_PREALLOC_RETAINED);
    owner.backing.protect(address,bytes,permission);
    /* SDK SetMemoryPermission is void/abort. Only its actual normal return is
     * accepted. A fault or lost execution leaves RETAINED, never a fake Result. */
    if(!immutable() || !receipt_identity(owner.receipt,&owner.saved) || receipt_state()!=PS2_PREALLOC_RETAINED ||
       owner.receipt->claims!=1 || owner.receipt->free_calls || owner.receipt->last_error!=PROTECT_IN_FLIGHT)fatal(PS2_MEMORY_POISONED);
    owner.receipt->last_error=0;state(PS2_PREALLOC_CLAIMED);
}
static void release(void *address){
    if((uintptr_t)address!=owner.saved.base || !live())fatal(PS2_MEMORY_POISONED);
    owner.free_started=1;owner.receipt->free_calls=1;
    owner.receipt->last_error=FREE_IN_FLIGHT;state(PS2_PREALLOC_RETAINED);
    owner.backing.release(address);
    if(!immutable() || !receipt_identity(owner.receipt,&owner.saved) || receipt_state()!=PS2_PREALLOC_RETAINED ||
       owner.receipt->claims!=1 || owner.receipt->free_calls!=1 || owner.receipt->last_error!=FREE_IN_FLIGHT)fatal(PS2_MEMORY_POISONED);
    owner.receipt->last_error=0;state(PS2_PREALLOC_RELEASED);
}
static int validate(const Ps2HbxlPreallocatedMain *d,const struct hbx_ctx *ctx,uintptr_t base){
    Range ranges[7];
    if(!d || !ctx || !pages((uintptr_t)d,4096,&ranges[5]) || !pages((uintptr_t)ctx,4096,&ranges[3]))return 0;
    if(ctx->magic!=HBX_ABI_MAGIC || ctx->abi_version!=HBX_ABI_VERSION || ctx->size!=sizeof(*ctx) ||
       ctx->args[4]!=(uintptr_t)d || !base || ctx->audio.initialize_parameter!=base+PS2_HBXL_AUDIO_INIT_OFFSET ||
       d->magic!=PS2_PREALLOC_MAIN_MAGIC || d->version!=1 || d->size!=136 || d->flags!=1 ||
       d->descriptor_self!=(uintptr_t)d || d->bytes!=PS2_HBXL_MAIN_DATA_BYTES ||
       d->code_base!=ctx->code_base || d->code_bytes!=ctx->code_size ||
       d->data_base!=ctx->data_base || d->data_bytes!=ctx->data_size ||
       d->context!=(uintptr_t)ctx || d->context_bytes!=4096 || d->sdk_base!=base ||
       d->sdk_bytes!=PS2_PREALLOC_SDK_RX_BYTES || d->receipt_bytes!=112 ||
       (!d->nonce[0] && !d->nonce[1]) ||
       !pages(d->base,d->bytes,&ranges[0]) || !pages(d->code_base,d->code_bytes,&ranges[1]) ||
       !pages(d->data_base,d->data_bytes,&ranges[2]) || !pages(d->sdk_base,d->sdk_bytes,&ranges[4]) ||
       !pages(d->receipt,4096,&ranges[6]))return 0;
    for(unsigned i=0;i<7;i++)for(unsigned j=i+1;j<7;j++)if(overlaps(ranges[i],ranges[j]))return 0;
    /* Native metadata is inside staticRW by design, so it is not another
     * independent image range. It must never be Main/descriptor/receipt. */
    Range metadata={(uintptr_t)&owner,sizeof(owner)};
    if(overlaps(ranges[0],metadata) || overlaps(ranges[5],metadata) || overlaps(ranges[6],metadata))return 0;
    const Ps2HbxlPreallocatedReceipt *r=(const void*)(uintptr_t)d->receipt;
    return receipt_identity(r,d) && __atomic_load_n(&r->state,__ATOMIC_ACQUIRE)==PS2_PREALLOC_PREPARED &&
        !r->claims && !r->free_calls && !r->last_error;
}
uint32_t ps2_hbxl_data_initialize_callbacks(const Ps2HeapPagesApi *api,uintptr_t base){
    (void)api;(void)base;return failed(PS2_MEMORY_UNSUPPORTED);
}
uint32_t ps2_hbxl_preallocated_initialize_callbacks(const Ps2HbxlPreallocatedMain *d,const struct hbx_ctx *ctx,const Ps2HeapPagesApi *api,uintptr_t base){
    if(owner.ever_bound || owner.ready || owner.heap.ready || owner.memory.ready || quarantined() || ps2_armsx2_memory_bound())return failed(PS2_MEMORY_BUSY);
    if(!api || !api->release || !api->protect || !api->fatal || !validate(d,ctx,base))return failed(PS2_MEMORY_INVALID);
    owner.saved=*d;owner.descriptor=d;owner.receipt=(void*)(uintptr_t)d->receipt;owner.backing=*api;
    Ps2HeapPagesApi wrapped={claim,release,protect,fatal};
    uint32_t result=ps2_heap_pages_bind(&owner.heap,&wrapped,PS2_HBXL_MAIN_DATA_BYTES);
    if(result)return failed(result);
    result=ps2_sdk_memory_initialize(&owner.memory,ps2_heap_pages_memory_api());
    if(!result)result=ps2_armsx2_bind_memory(&owner.memory);
    if(result){
        if(owner.heap.active || owner.memory.active || quarantined())return failed(PS2_MEMORY_POISONED);
        uint32_t cleanup=ps2_heap_pages_unbind(&owner.heap);if(cleanup)return failed(cleanup);
        memset(&owner.memory,0,sizeof(owner.memory));return failed(result);
    }
    owner.sdk_base=base;owner.ever_bound=1;owner.ready=1;owner.last_error=0;return 0;
}
uint32_t ps2_hbxl_data_initialize(void){
    if(owner.ever_bound || owner.ready || owner.heap.ready || owner.memory.ready || quarantined())return failed(PS2_MEMORY_BUSY);
    const struct hbx_ctx *ctx=hbxl_ctx;Range context;
    if(!ctx || !pages((uintptr_t)ctx,4096,&context) || ctx->magic!=HBX_ABI_MAGIC || ctx->abi_version!=HBX_ABI_VERSION ||
       ctx->size!=sizeof(*ctx) || ctx->audio.initialize_parameter<PS2_HBXL_AUDIO_INIT_OFFSET)return failed(PS2_MEMORY_UNSUPPORTED);
    uintptr_t base=(uintptr_t)(ctx->audio.initialize_parameter-PS2_HBXL_AUDIO_INIT_OFFSET);Range sdk;
    if(!pages(base,PS2_PREALLOC_SDK_RX_BYTES,&sdk) ||
       hbxl_imp[IMP_MEMALIGN]!=base+PS2_HBXL_MEMALIGN_OFFSET || hbxl_imp[IMP_FREE]!=base+PS2_HBXL_DELETE_OFFSET)return failed(PS2_MEMORY_UNSUPPORTED);
    /* Verify the same public SDK byte cuts as the original provider. The
     * allocator import is an identity check only and is NEVER called here. */
    if(memcmp((const void*)(base+PS2_HBXL_MEMALIGN_OFFSET),ps2_hbxl_memalign_bytes,sizeof(ps2_hbxl_memalign_bytes)) ||
       memcmp((const void*)(base+PS2_HBXL_DELETE_OFFSET),ps2_hbxl_delete_bytes,sizeof(ps2_hbxl_delete_bytes)) ||
       memcmp((const void*)(base+PS2_HBXL_PERMISSION_OFFSET),ps2_hbxl_permission_bytes,sizeof(ps2_hbxl_permission_bytes)))return failed(PS2_MEMORY_UNSUPPORTED);
    Ps2HeapPagesApi api={0,(void(*)(void*))(base+PS2_HBXL_DELETE_OFFSET),
        (void(*)(uintptr_t,size_t,unsigned))(base+PS2_HBXL_PERMISSION_OFFSET),native_fatal};
    uint32_t result=ps2_hbxl_preallocated_initialize_callbacks((const void*)(uintptr_t)ctx->args[4],ctx,&api,base);
    if(!result)owner.fingerprints=1;return result;
}
Ps2SdkMemory *ps2_hbxl_data_manager(void){return owner.ready?&owner.memory:0;}
uint32_t ps2_hbxl_data_allocate(Ps2MemoryRegion *region,size_t bytes,unsigned char *states,size_t capacity){
    if(!owner.ready)return failed(PS2_MEMORY_UNSUPPORTED);
    if(quarantined())return failed(PS2_MEMORY_POISONED);
    if(bytes!=PS2_HBXL_MAIN_DATA_BYTES)return failed(PS2_MEMORY_INVALID);
    uint32_t result=ps2_sdk_memory_reserve(&owner.memory,bytes,states,capacity,region);
    if(!result)result=ps2_sdk_memory_commit(region,0,bytes);
    return result?failed(result):0;
}
uint32_t ps2_hbxl_data_release(Ps2MemoryRegion *region){
    if(!owner.ready || !region || region->owner!=&owner.memory)return failed(PS2_MEMORY_INVALID);
    if(quarantined() || region->poisoned)return failed(PS2_MEMORY_POISONED);
    uint32_t result=ps2_sdk_memory_decommit(region,0,region->bytes);
    if(!result)result=ps2_sdk_memory_release(region);
    return result?failed(result):0;
}
uint32_t ps2_hbxl_data_shutdown(void){
    if(!owner.ready)return failed(PS2_MEMORY_INVALID);
    if(quarantined())return failed(PS2_MEMORY_POISONED);
    if(owner.heap.active || owner.heap.live_bytes || owner.memory.active || owner.memory.committed_pages)return failed(PS2_MEMORY_BUSY);
    if(owner.claimed && (receipt_state()!=PS2_PREALLOC_RELEASED || owner.receipt->free_calls!=1 || owner.receipt->last_error))return failed(PS2_MEMORY_POISONED);
    uint32_t result=ps2_armsx2_bind_memory(0);if(result)return failed(result);
    result=ps2_heap_pages_unbind(&owner.heap);if(result)return failed(result);
    /* Keep the one-shot/replay and receipt evidence. Nothing prepares or frees
     * another block implicitly when this image is initialized a second time. */
    owner.ready=0;owner.last_error=0;return 0;
}
void ps2_hbxl_data_statistics(Ps2HbxlDataStatistics *out){
    if(!out)return;
    *out=(Ps2HbxlDataStatistics){.sdk_base=owner.sdk_base,.live_bytes=owner.heap.live_bytes,.committed_pages=owner.memory.committed_pages,
        .ready=owner.ready,.quarantined=quarantined(),.last_error=owner.last_error,.sdk_fingerprints_verified=owner.fingerprints,
        .active_heap_blocks=owner.heap.active,.active_regions=owner.memory.active,.allocations=owner.heap.allocations,.releases=owner.heap.releases,
        .commits=owner.heap.commits,.decommits=owner.heap.decommits,.protections=owner.heap.protections};
}
