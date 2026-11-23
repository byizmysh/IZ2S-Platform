/* Isolated two-domain writer serialization. Original code_host.c is unchanged. */
#include "hbxl_code_domains.h"
#include <string.h>
#define SCOPES 32u
static struct {
    Ps2CodeArena *arena;
    Ps2CodeHostApi api;
    void *base;
    size_t bytes;
    uint64_t owner;
    uint32_t gate,depth;
    struct {uintptr_t start;size_t bytes;} scopes[SCOPES];
} host;
int ps2_code_host_bind(Ps2CodeArena *arena,const Ps2CodeHostApi *api) {
    if(host.base || host.depth || (host.arena && host.arena->poisoned))return PS2_CODE_BUSY;
    if(!arena){memset(&host,0,sizeof(host));return 0;}
    if(!api || !api->current_thread || !api->enter_publication || !api->leave_publication ||
       arena->poisoned || arena->live_pages || !arena->bytes)return PS2_CODE_INVALID;
    memset(&host,0,sizeof(host));host.arena=arena;host.api=*api;return 0;
}
int ps2_code_host_bound(void){return host.arena!=0;}
int ps2_code_host_reserve(size_t bytes,void **out) {
    if(out)*out=0;
    if(!host.arena || !out || host.base || host.depth || !bytes || bytes%PS2_PAGE)return PS2_CODE_INVALID;
    int result=ps2_arena_reserve_lazy(host.arena,bytes,out);
    if(!result){host.base=*out;host.bytes=bytes;}return result;
}
int ps2_code_host_begin(void *base,size_t bytes) {
    if(!host.base || !host.arena)return PS2_CODE_INVALID;
    uint64_t self=host.api.current_thread(host.api.opaque);if(!self)return PS2_CODE_INVALID;
    if(!ps2_code_domains_scope_allowed((uintptr_t)base,bytes))return PS2_CODE_INVALID;
    uint32_t expected;int acquired=0;
    for(unsigned waits=0;;waits++) {
        expected=0;
        if(__atomic_compare_exchange_n(&host.gate,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
            acquired=1;__atomic_store_n(&host.owner,self,__ATOMIC_RELEASE);break;
        }
        if(__atomic_load_n(&host.owner,__ATOMIC_ACQUIRE)==self)break;
        if(waits==10000 || ps2_code_domains_writer_wait())return PS2_CODE_BUSY;
    }
    if(host.arena->poisoned || !ps2_arena_writable_range(host.arena,base,bytes)) {
        if(acquired){__atomic_store_n(&host.owner,0,__ATOMIC_RELEASE);__atomic_store_n(&host.gate,0,__ATOMIC_RELEASE);}
        return PS2_CODE_INVALID;
    }
    if(host.depth==SCOPES)return PS2_CODE_BUSY;
    host.scopes[host.depth].start=(uintptr_t)base;host.scopes[host.depth].bytes=bytes;host.depth++;return 0;
}
static int owner(void) {
    return host.arena && __atomic_load_n(&host.owner,__ATOMIC_ACQUIRE)==host.api.current_thread(host.api.opaque) && host.depth;
}
void *ps2_code_host_writable(const void *base,size_t bytes) {
    if(!owner())return 0;
    uintptr_t p=(uintptr_t)base;
    for(uint32_t i=host.depth;i;i--) {
        uintptr_t start=host.scopes[i-1].start;size_t n=host.scopes[i-1].bytes;
        if(p>=start && p-start<n && bytes<=n-(p-start))return ps2_arena_writable_range(host.arena,base,bytes);
    }
    return 0;
}
void *ps2_code_host_translate(const void *base,size_t bytes) {
    return host.base?ps2_arena_writable_range(host.arena,base,bytes):0;
}
int ps2_code_host_end(void *base,size_t bytes) {
    if(!owner() || host.scopes[host.depth-1].start!=(uintptr_t)base || !bytes || bytes>host.scopes[host.depth-1].bytes)return PS2_CODE_INVALID;
    uintptr_t start=(uintptr_t)base&~(uintptr_t)(PS2_PAGE-1u);
    size_t span=(((uintptr_t)base-start)+bytes+PS2_PAGE-1u)&~(size_t)(PS2_PAGE-1u);
    int excluded=host.api.enter_publication_range?
        host.api.enter_publication_range(host.api.opaque,start,span):host.api.enter_publication(host.api.opaque);
    if(excluded)return PS2_CODE_BUSY;
    int result=ps2_arena_publish_range(host.arena,base,bytes);
    host.api.leave_publication(host.api.opaque);
    if(result)return result;
    if(!--host.depth){__atomic_store_n(&host.owner,0,__ATOMIC_RELEASE);__atomic_store_n(&host.gate,0,__ATOMIC_RELEASE);}
    return 0;
}
int ps2_code_host_begin_all(void){return ps2_code_host_begin(host.base,host.bytes);}
int ps2_code_host_end_all(void){return ps2_code_host_end(host.base,host.bytes);}
int ps2_code_host_release(void) {
    if(!host.base || host.depth)return PS2_CODE_INVALID;
    if(host.api.enter_publication(host.api.opaque))return PS2_CODE_BUSY;
    int result=ps2_arena_release(host.arena,host.base,host.bytes);
    host.api.leave_publication(host.api.opaque);
    if(!result){host.base=0;host.bytes=0;}return result;
}
