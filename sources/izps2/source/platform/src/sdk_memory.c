#include "sdk_memory.h"
#include <string.h>

static int aligned(size_t n) { return !(n&(PS2_MEMORY_PAGE-1)); }
static int range(uintptr_t p,size_t n) { return p && n && aligned(p) && aligned(n) && n<=UINTPTR_MAX-p; }
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    return a<=b ? b-a<an : a-b<bn;
}
static int usable(const Ps2SdkMemory *m) { return m && m->ready && !m->poisoned; }
static int registered(const Ps2MemoryRegion *r) {
    if (!r || !usable(r->owner) || r->poisoned) return 0;
    for (unsigned i=0;i<PS2_MEMORY_REGIONS;i++) if (r->owner->regions[i]==r) return 1;
    return 0;
}
static uint32_t uncertain(Ps2MemoryRegion *r,uint32_t result) {
    r->poisoned=1;r->owner->last_error=result;return result;
}
uint32_t ps2_sdk_memory_initialize(Ps2SdkMemory *m,const Ps2SdkMemoryApi *api) {
    if (!m || !api || !api->is_enabled || !api->reserve || !api->commit || !api->decommit || !api->release || !api->protect)
        return PS2_MEMORY_INVALID;
    /* Caller supplies zero-initialized storage. Never reset live ownership. */
    if (m->ready || m->active || m->poisoned) return PS2_MEMORY_BUSY;
    if (!api->is_enabled()) return PS2_MEMORY_UNSUPPORTED;
    memset(m,0,sizeof(*m));m->api=*api;m->ready=1;return 0;
}
uint32_t ps2_sdk_memory_reserve(Ps2SdkMemory *m,size_t bytes,unsigned char *states,size_t capacity,Ps2MemoryRegion *out) {
    if (!usable(m)) return PS2_MEMORY_POISONED;
    if (!out || overlap((uintptr_t)out,sizeof(*out),(uintptr_t)m,sizeof(*m)) || out->owner || !bytes || !aligned(bytes) || !states || capacity<bytes/PS2_MEMORY_PAGE ||
        capacity>UINTPTR_MAX-(uintptr_t)states || overlap((uintptr_t)states,capacity,(uintptr_t)out,sizeof(*out)) ||
        overlap((uintptr_t)states,capacity,(uintptr_t)m,sizeof(*m))) return PS2_MEMORY_INVALID;
    unsigned slot=PS2_MEMORY_REGIONS;
    for (unsigned i=0;i<PS2_MEMORY_REGIONS;i++) {
        Ps2MemoryRegion *r=m->regions[i];
        if (!r) { if(slot==PS2_MEMORY_REGIONS)slot=i;continue; }
        if (r==out || overlap((uintptr_t)states,capacity,(uintptr_t)r->states,r->pages) ||
            overlap((uintptr_t)out,sizeof(*out),(uintptr_t)r,sizeof(*r)) ||
            overlap((uintptr_t)states,capacity,(uintptr_t)r,sizeof(*r)) ||
            overlap((uintptr_t)out,sizeof(*out),(uintptr_t)r->states,r->pages) ||
            overlap((uintptr_t)states,capacity,r->base,r->bytes) ||
            overlap((uintptr_t)out,sizeof(*out),r->base,r->bytes)) return PS2_MEMORY_INVALID;
    }
    if(slot==PS2_MEMORY_REGIONS)return PS2_MEMORY_BUSY;
    uintptr_t address=0;m->calls++;
    uint32_t result=m->api.reserve(&address,bytes);
    if(result){m->last_error=result;if(address)m->poisoned=1;return result;}
    int valid=range(address,bytes) && !overlap(address,bytes,(uintptr_t)states,capacity) &&
        !overlap(address,bytes,(uintptr_t)out,sizeof(*out)) && !overlap(address,bytes,(uintptr_t)m,sizeof(*m));
    for (unsigned i=0;i<PS2_MEMORY_REGIONS && valid;i++) {
        Ps2MemoryRegion *r=m->regions[i];
        if(r && (overlap(address,bytes,r->base,r->bytes) || overlap(address,bytes,(uintptr_t)r,sizeof(*r)) ||
                 overlap(address,bytes,(uintptr_t)r->states,r->pages))) valid=0;
    }
    if(!valid){m->poisoned=1;m->last_error=PS2_MEMORY_BAD_REPLY;return PS2_MEMORY_BAD_REPLY;}
    memset(states,0,bytes/PS2_MEMORY_PAGE);
    *out=(Ps2MemoryRegion){.owner=m,.base=address,.bytes=bytes,.pages=bytes/PS2_MEMORY_PAGE,.states=states};
    m->regions[slot]=out;m->active++;m->reservations++;return 0;
}
static uint32_t check(Ps2MemoryRegion *r,size_t offset,size_t bytes,int committed) {
    if(!registered(r))return PS2_MEMORY_POISONED;
    if(!bytes || !aligned(offset) || !aligned(bytes) || offset>=r->bytes || bytes>r->bytes-offset)return PS2_MEMORY_INVALID;
    for(size_t i=offset/PS2_MEMORY_PAGE;i<(offset+bytes)/PS2_MEMORY_PAGE;i++)
        if(!!r->states[i]!=committed)return PS2_MEMORY_INVALID;
    return 0;
}
uint32_t ps2_sdk_memory_commit(Ps2MemoryRegion *r,size_t offset,size_t bytes) {
    uint32_t result=check(r,offset,bytes,0);if(result)return result;
    Ps2SdkMemory *m=r->owner;m->calls++;
    result=m->api.commit(r->base+offset,bytes);if(result)return uncertain(r,result);
    memset(r->states+offset/PS2_MEMORY_PAGE,0x83,bytes/PS2_MEMORY_PAGE);
    r->committed+=bytes/PS2_MEMORY_PAGE;m->committed_pages+=bytes/PS2_MEMORY_PAGE;m->commits++;return 0;
}
uint32_t ps2_sdk_memory_decommit(Ps2MemoryRegion *r,size_t offset,size_t bytes) {
    uint32_t result=check(r,offset,bytes,1);if(result)return result;
    Ps2SdkMemory *m=r->owner;m->calls++;
    result=m->api.decommit(r->base+offset,bytes);if(result)return uncertain(r,result);
    memset(r->states+offset/PS2_MEMORY_PAGE,0,bytes/PS2_MEMORY_PAGE);
    r->committed-=bytes/PS2_MEMORY_PAGE;m->committed_pages-=bytes/PS2_MEMORY_PAGE;m->decommits++;return 0;
}
uint32_t ps2_sdk_memory_protect(Ps2SdkMemory *m,uintptr_t base,size_t bytes,unsigned permission) {
    if(!usable(m))return PS2_MEMORY_POISONED;
    if(permission!=0 && permission!=1 && permission!=3)return PS2_MEMORY_UNSUPPORTED;
    if(!range(base,bytes))return PS2_MEMORY_INVALID;
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++) {
        Ps2MemoryRegion *r=m->regions[i];if(!r || base<r->base || base-r->base>=r->bytes)continue;
        size_t offset=base-r->base;uint32_t result=check(r,offset,bytes,1);if(result)return result;
        m->calls++;m->api.protect(base,bytes,permission);
        memset(r->states+offset/PS2_MEMORY_PAGE,0x80|permission,bytes/PS2_MEMORY_PAGE);m->protections++;return 0;
    }
    return PS2_MEMORY_INVALID;
}
uint32_t ps2_sdk_memory_release(Ps2MemoryRegion *r) {
    if(!registered(r))return PS2_MEMORY_POISONED;
    if(r->committed)return PS2_MEMORY_BUSY;
    Ps2SdkMemory *m=r->owner;m->calls++;uint32_t result=m->api.release(r->base);
    if(result)return uncertain(r,result);
    for(unsigned i=0;i<PS2_MEMORY_REGIONS;i++)if(m->regions[i]==r){m->regions[i]=0;break;}
    m->active--;m->releases++;memset(r,0,sizeof(*r));return 0;
}
