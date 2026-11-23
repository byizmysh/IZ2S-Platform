/* Physical GS VRAM aliasing. No POSIX shared files or memory-copy mirrors. */
#include "gs_shared_vram.h"
#include <string.h>
_Static_assert(sizeof(Ps2GsMemoryInfo)==40,"kernel MemoryInfo ABI");
_Static_assert(sizeof(Ps2GsMirrorStatistics)==80,"mirror statistics ABI");
static Ps2GsMirrorApi api;
Ps2GsMirrorStatistics ps2_gs_mirror_stats;
static struct Slot {uintptr_t base;size_t size,repeat,mapped;uint32_t handle,allocated;} slots[16];
#define S ps2_gs_mirror_stats
#define LIMIT (1ull<<39)
static int lock(void){uint32_t zero=0;return __atomic_compare_exchange_n(&S.busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
static uint32_t done(uint32_t error){S.last_error=error;__atomic_store_n(&S.busy,0,__ATOMIC_RELEASE);return error;}
static int valid(uint64_t p,uint64_t n){return p&&!(p&4095)&&n&&!(n&4095)&&p<LIMIT&&n<=LIMIT-p;}
static int overlap(uint64_t p,uint64_t n,uint64_t q,uint64_t m){return p<q+m&&q<p+n;}
uint32_t ps2_gs_mirror_bind(const Ps2GsMirrorApi*a){
    if(!lock())return PS2_GS_MIRROR_BUSY;
    if(S.configured||S.poisoned||S.live_objects)return done(PS2_GS_MIRROR_BUSY);
    if(!a||!a->create||!a->map||!a->unmap||!a->close||!a->query||!a->info)return done(PS2_GS_MIRROR_INVALID);
    api=*a;S.configured=1;return done(0);
}
static uintptr_t find_free(size_t bytes){
    uint64_t ranges[4][2]={{0}};const uint32_t types[4][2]={{12,13},{2,3},{4,5},{14,15}};
    for(unsigned i=0;i<4;i++){
        for(unsigned j=0;j<2;j++)if(api.info(&ranges[i][j],types[i][j],0xffff8001u,0))return 0;
        if(!valid(ranges[i][0],ranges[i][1]))return 0;
    }
    const uint64_t align=bytes>=2097152?2097152:4096,guard=65536;
    uint64_t at=(ranges[0][0]+guard+align-1)&~(align-1),end=ranges[0][0]+ranges[0][1];
    for(unsigned count=0;count<32768&&at<end&&bytes+guard<=end-at;count++){
        uint64_t skip=at;
        for(unsigned i=1;i<4;i++)if(overlap(at-guard,bytes+guard*2,ranges[i][0],ranges[i][1])){
            uint64_t next=ranges[i][0]+ranges[i][1]+guard;if(next>skip)skip=next;
        }
        if(skip!=at){at=(skip+align-1)&~(align-1);continue;}
        uint64_t cursor=at-guard,until=at+bytes+guard;int free=1;
        for(unsigned q=0;cursor<until&&q<32768;q++){
            Ps2GsMemoryInfo m={0};uint32_t page=0;
            if(api.query(&m,&page,(uintptr_t)cursor)||!m.bytes||m.base>cursor||m.base>=LIMIT||m.bytes>LIMIT-m.base||cursor-m.base>=m.bytes)return 0;
            const uint64_t next=m.base+m.bytes;
            if(m.type||m.permission||m.attributes||m.device_refs||m.ipc_refs){at=(next+guard+align-1)&~(align-1);free=0;break;}
            cursor=next<until?next:until;
        }
        if(free&&cursor==until)return (uintptr_t)at;
        if(free)return 0;
    }
    return 0;
}
/* Called with the allocator locked. Each successful syscall retires exactly
 * one owned resource. A failed syscall preserves every remaining resource. */
static uint32_t retire(struct Slot*s){
    while(s->mapped){
        uint32_t error=api.unmap(s->handle,s->base+(s->mapped-1)*s->size,s->size);
        if(error){S.poisoned=1;return error;}
        s->mapped--;S.unmaps++;S.live_mappings--;
    }
    uint32_t error=api.close(s->handle);
    if(error){S.poisoned=1;return error;}
    S.closes++;S.live_objects--;if(s->allocated)S.frees++;
    memset(s,0,sizeof(*s));return 0;
}
void *ps2_gs_mirror_allocate(size_t size,size_t repeat){
    if(!lock())return 0;
    if(!S.configured||S.poisoned){done(PS2_GS_MIRROR_BUSY);return 0;}
    if(!size||(size&4095)||!repeat||repeat>4||size>64u*1024*1024/repeat){done(PS2_GS_MIRROR_INVALID);return 0;}
    struct Slot*s=0;for(unsigned i=0;i<16;i++)if(!slots[i].handle){s=&slots[i];break;}
    if(!s){done(PS2_GS_MIRROR_BUSY);return 0;}
    uintptr_t base=find_free(size*repeat);
    if(!base){done(PS2_GS_MIRROR_RANGE);return 0;}
    uint32_t handle=0,error=api.create(&handle,size,3,3);
    if(error){done(error);return 0;} /* W1 is unspecified on failed Create. */
    if(!handle){S.poisoned=1;done(PS2_GS_MIRROR_SDK);return 0;}
    *s=(struct Slot){base,size,repeat,0,handle,0};S.creates++;S.live_objects++;
    for(size_t i=0;i<repeat;i++){
        error=api.map(handle,base+i*size,size,3);
        if(error){uint32_t cleanup=retire(s);done(cleanup?cleanup:error);return 0;}
        s->mapped++;S.maps++;S.live_mappings++;
    }
    s->allocated=1;S.allocations++;done(0);return (void*)base;
}
uint32_t ps2_gs_mirror_release(void*p,size_t size,size_t repeat){
    if(!lock())return PS2_GS_MIRROR_BUSY;
    if(!S.configured||S.poisoned)return done(PS2_GS_MIRROR_BUSY);
    struct Slot*s=0;for(unsigned i=0;i<16;i++)if(slots[i].handle&&slots[i].base==(uintptr_t)p){s=&slots[i];break;}
    if(!s||!s->allocated||s->size!=size||s->repeat!=repeat)return done(PS2_GS_MIRROR_INVALID);
    return done(retire(s));
}
