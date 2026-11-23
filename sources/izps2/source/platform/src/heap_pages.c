#include "heap_pages.h"
#include <string.h>
static Ps2HeapPages *bound;
static int aligned(size_t n) {return n && !(n&(PS2_MEMORY_PAGE-1));}
static int valid(void) {return bound && bound->ready && !bound->poisoned;}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    return a<=b ? b-a<an : a-b<bn;
}
static Ps2HeapBlock *containing(uintptr_t address,size_t bytes) {
    if(!valid() || !aligned(address) || !aligned(bytes) || bytes>UINTPTR_MAX-address)return 0;
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++) {
        Ps2HeapBlock *b=&bound->blocks[i];
        if(b->base && address>=b->base && address-b->base<b->bytes && bytes<=b->bytes-(address-b->base))return b;
    }
    return 0;
}
static bool enabled(void) {return valid()!=0;}
__attribute__((noreturn)) static void fatal(uint32_t error) {
    if(bound){bound->last_error=error;bound->poisoned=1;bound->api.fatal(error);}
    __builtin_trap();
}
static uint32_t reserve(uintptr_t *out,size_t bytes) {
    if(!out || !valid() || !aligned(bytes))return PS2_HEAP_INVALID;
    *out=0;
    if(bound->live_bytes>bound->limit_bytes || bytes>bound->limit_bytes-bound->live_bytes)return PS2_HEAP_LIMIT;
    Ps2HeapBlock *slot=0;
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++)if(!bound->blocks[i].base){slot=&bound->blocks[i];break;}
    if(!slot)return PS2_HEAP_LIMIT;
    void *p=0;int result=bound->api.allocate(&p,PS2_MEMORY_PAGE,bytes);
    if(result && !p){bound->last_error=(uint32_t)result;return (uint32_t)result;}
    /* Retain even a malformed reply. Never free or protect an untrusted range. */
    uintptr_t address=(uintptr_t)p;
    if(result || !aligned(address) || bytes>UINTPTR_MAX-address ||
       overlap(address,bytes,(uintptr_t)bound,sizeof(*bound)) ||
       overlap(address,bytes,(uintptr_t)out,sizeof(*out))) {
        *out=address;slot->base=address;slot->bytes=bytes;bound->poisoned=1;
        bound->last_error=PS2_HEAP_BAD_REPLY;return PS2_HEAP_BAD_REPLY;
    }
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++) {
        Ps2HeapBlock *b=&bound->blocks[i];if(!b->base)continue;
        if(address<=b->base ? b->base-address<bytes : address-b->base<b->bytes) {
            bound->poisoned=1;bound->last_error=PS2_HEAP_BAD_REPLY;*out=address;return PS2_HEAP_BAD_REPLY;
        }
    }
    *slot=(Ps2HeapBlock){address,bytes};bound->active++;bound->live_bytes+=bytes;bound->allocations++;
    *out=address;bound->api.protect(address,bytes,0);return 0;
}
static uint32_t commit(uintptr_t address,size_t bytes) {
    if(!containing(address,bytes))return PS2_HEAP_INVALID;
    bound->api.protect(address,bytes,3);memset((void*)address,0,bytes);bound->commits++;return 0;
}
static uint32_t decommit(uintptr_t address,size_t bytes) {
    if(!containing(address,bytes))return PS2_HEAP_INVALID;
    bound->api.protect(address,bytes,0);bound->decommits++;return 0;
}
static uint32_t release(uintptr_t address) {
    if(!valid())return PS2_HEAP_INVALID;
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++) {
        Ps2HeapBlock *b=&bound->blocks[i];if(!b->base || b->base!=address)continue;
        size_t bytes=b->bytes;
        // Allocators may access the payload while freeing/coalescing a block.
        bound->api.protect(address,bytes,3);bound->api.release((void*)address);
        memset(b,0,sizeof(*b));bound->active--;bound->live_bytes-=bytes;bound->releases++;return 0;
    }
    return PS2_HEAP_INVALID;
}
static void protect(uintptr_t address,size_t bytes,unsigned permission) {
    if((permission!=0 && permission!=1 && permission!=3) || !containing(address,bytes))fatal(PS2_HEAP_INVALID);
    bound->api.protect(address,bytes,permission);bound->protections++;
}
static const Ps2SdkMemoryApi memory_api={enabled,reserve,commit,decommit,release,protect};
uint32_t ps2_heap_pages_bind(Ps2HeapPages *p,const Ps2HeapPagesApi *api,size_t limit) {
    if(!p || !api || !api->allocate || !api->release || !api->protect || !api->fatal || !aligned(limit))return PS2_HEAP_INVALID;
    if(bound || p->ready || p->active || p->poisoned)return PS2_HEAP_BUSY;
    Ps2HeapPagesApi saved=*api;
    memset(p,0,sizeof(*p));p->api=saved;p->limit_bytes=limit;p->ready=1;bound=p;return 0;
}
uint32_t ps2_heap_pages_unbind(Ps2HeapPages *p) {
    if(!p || bound!=p || !p->ready)return PS2_HEAP_INVALID;
    if(p->active || p->live_bytes || p->poisoned)return PS2_HEAP_BUSY;
    p->ready=0;bound=0;return 0;
}
const Ps2SdkMemoryApi *ps2_heap_pages_memory_api(void) {return &memory_api;}
