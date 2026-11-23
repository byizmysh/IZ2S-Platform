#include "hbxl_code_memory.h"
#include <string.h>
_Static_assert(sizeof(Ps2HbxlCodeConfig)==80,"HBXL code config ABI");
_Static_assert(sizeof(Ps2HbxlMemoryInfo)==40,"Horizon MemoryInfo ABI");
static struct {
    Ps2CodeArena arena;
    Ps2RoBackend ro;
    Ps2HbxlCodeApi api;
    uint64_t owner;
    uint32_t initialized,publishing;
} state;
static int valid(uint64_t p,uint64_t n){return p && !(p&4095) && n && !(n&4095) && p<(1ull<<39) && n<=(1ull<<39)-p;}
static int overlap(uint64_t p,uint64_t n,uint64_t q,uint64_t m){return p<q+m && q<p+n;}
static int covers(const Ps2HbxlCodeApi *api,uint64_t base,uint64_t bytes,int free_va){
    const uint64_t end=base+bytes;
    for(uint64_t p=base;p<end;){
        Ps2HbxlMemoryInfo m;uint32_t page=0;memset(&m,0,sizeof m);
        if(api->query(&m,&page,p) || !m.bytes || m.base>p || m.base>=(1ull<<39) || m.bytes>(1ull<<39)-m.base || p-m.base>=m.bytes)return 0;
        if(free_va ? (m.type!=0 || m.permission || m.attributes) :
                      (m.type!=5 || m.permission!=3 || m.attributes || m.ipc_refs || m.device_refs))return 0;
        const uint64_t next=m.base+m.bytes;if(next<=p)return 0;p=next<end?next:end;
    }
    return 1;
}
static uint64_t self(void *opaque){(void)opaque;return state.api.current();}
static int enter(void *opaque){
    (void)opaque;
    if(!state.initialized || state.publishing || state.api.current()!=state.owner)return 1;
    state.publishing=1;return 0;
}
static void leave(void *opaque){(void)opaque;state.publishing=0;}
static int range_enter(void *opaque,uintptr_t base,size_t bytes){
    if(!bytes || (base&4095) || (bytes&4095) || base<(uintptr_t)state.arena.rx ||
       base-(uintptr_t)state.arena.rx>=state.arena.bytes || bytes>state.arena.bytes-(base-(uintptr_t)state.arena.rx))return 1;
    return enter(opaque);
}
uint32_t ps2_hbxl_code_bind(const Ps2HbxlCodeConfig *config,const Ps2HbxlCodeApi *api){
    if(state.initialized || state.arena.poisoned || ps2_code_host_bound())return PS2_HBXL_CODE_BUSY;
    if(!config || !api || !api->query || !api->send || !api->current || !api->clean || !api->invalidate)return PS2_HBXL_CODE_INVALID;
    Ps2HbxlCodeConfig c=*config;Ps2HbxlCodeApi a=*api;
    if(c.magic!=PS2_HBXL_CODE_MAGIC || c.version!=1 || c.size!=sizeof c ||
       c.flags!=(PS2_HBXL_CODE_SINGLE_EXECUTOR|PS2_HBXL_CODE_EXCLUSIVE_RO) || c.reserved ||
       !c.pid || !c.ro_pid || c.pid==c.ro_pid || !c.send || c.send!=(uintptr_t)a.send || !c.service || c.bytes>PS2_MAX_PAGES*(uint64_t)PS2_PAGE ||
       !valid(c.source,c.bytes) || !valid(c.shadow,c.bytes) || !valid(c.rx,c.bytes) ||
       overlap(c.source,c.bytes,c.shadow,c.bytes) || overlap(c.source,c.bytes,c.rx,c.bytes) || overlap(c.shadow,c.bytes,c.rx,c.bytes) ||
       overlap((uintptr_t)&state,sizeof state,c.source,c.bytes) || overlap((uintptr_t)&state,sizeof state,c.shadow,c.bytes) ||
       overlap((uintptr_t)config,sizeof *config,c.source,c.bytes) || overlap((uintptr_t)config,sizeof *config,c.shadow,c.bytes) ||
       overlap((uintptr_t)api,sizeof *api,c.source,c.bytes) || overlap((uintptr_t)api,sizeof *api,c.shadow,c.bytes))return PS2_HBXL_CODE_INVALID;
    if(!a.current())return PS2_HBXL_CODE_INVALID;
    if(!covers(&a,c.source,c.bytes,0) || !covers(&a,c.shadow,c.bytes,0) || !covers(&a,c.rx,c.bytes,1))return PS2_HBXL_CODE_RANGE;
    memset(&state,0,sizeof state);state.api=a;state.owner=a.current();
    if(!state.owner)return PS2_HBXL_CODE_INVALID;
    state.ro=(Ps2RoBackend){.pid=c.pid,.ro_pid=c.ro_pid,.service=c.service,.send=a.send,.source=c.source,.rx=c.rx,.bytes=c.bytes};
    if(ps2_arena_init(&state.arena,(void*)(uintptr_t)c.source,(void*)(uintptr_t)c.shadow,(void*)(uintptr_t)c.rx,c.bytes,
                      &state.ro,ps2_ro_map_page,ps2_ro_unmap_page,a.clean,a.invalidate))return PS2_HBXL_CODE_INVALID;
    const Ps2CodeHostApi host={&state,self,enter,leave,range_enter};
    state.initialized=1;
    if(ps2_code_host_bind(&state.arena,&host)){state.initialized=0;return PS2_HBXL_CODE_BUSY;}
    return 0;
}
uint32_t ps2_hbxl_code_shutdown(void){
    if(!state.initialized || state.api.current()!=state.owner || state.publishing || state.arena.poisoned ||
       state.arena.live_pages || state.arena.mapped_pages || state.arena.maps!=state.arena.unmaps)return PS2_HBXL_CODE_BUSY;
    if(ps2_code_host_bind(0,0))return PS2_HBXL_CODE_BUSY;
    state.initialized=0;return 0;
}
void ps2_hbxl_code_statistics(Ps2HbxlCodeStatistics *s){
    if(!s)return;
    *s=(Ps2HbxlCodeStatistics){(uintptr_t)state.arena.source,(uintptr_t)state.arena.shadow,(uintptr_t)state.arena.rx,state.arena.bytes,
        state.owner,state.arena.maps,state.arena.unmaps,state.arena.publications,state.initialized,state.arena.poisoned,
        state.arena.live_pages,state.arena.mapped_pages,state.arena.peak_pages,state.arena.last_error,state.ro.transport,state.ro.service_result};
}
