#include "hbxl_code_domains.h"
#include <string.h>
_Static_assert(sizeof(Ps2HbxlCodeDomainsConfig)==96,"domains config ABI");
static struct {
    Ps2CodeArena arena;
    Ps2RoBackend ro;
    Ps2HbxlCodeDomainsApi api;
    uint64_t ee,gs,gs_start,gs_end,ee_publications,gs_publications,writer_waits;
    uint32_t initialized,publishing,gs_joined,last_error;
} domain;
static int range(uint64_t p,uint64_t n){return p && !(p&4095) && n && !(n&4095) && p<(1ull<<39) && n<=(1ull<<39)-p;}
static int overlaps(uint64_t p,uint64_t n,uint64_t q,uint64_t m){return p<q+m && q<p+n;}
static int covers(const Ps2HbxlCodeApi*a,uint64_t p,uint64_t n,int free_va){
    const uint64_t end=p+n;
    while(p<end){Ps2HbxlMemoryInfo m={0};uint32_t page=0;
        if(a->query(&m,&page,p)||!m.bytes||m.base>p||m.base>=(1ull<<39)||m.bytes>(1ull<<39)-m.base||p-m.base>=m.bytes||m.attributes||m.ipc_refs||m.device_refs)return 0;
        if(free_va?(m.type!=0||m.permission):(m.type!=5||m.permission!=3))return 0;
        p=m.base+m.bytes;
    }return 1;
}
static uint64_t self(void*unused){(void)unused;return domain.api.code.current();}
static int belongs(uint64_t t,uintptr_t p,size_t n){
    if(!domain.initialized||!t||!n||p<(uintptr_t)domain.arena.rx||p-(uintptr_t)domain.arena.rx>=domain.arena.bytes||n>domain.arena.bytes-(p-(uintptr_t)domain.arena.rx))return 0;
    const uint64_t gs=__atomic_load_n(&domain.gs,__ATOMIC_ACQUIRE);
    if(t==domain.ee){
        if(p+n<=domain.gs_start)return 1;
        /* Initialization may touch the whole cache before GS starts, and
         * final cleanup may do so after its independently proven join. */
        return !gs || __atomic_load_n(&domain.gs_joined,__ATOMIC_ACQUIRE);
    }
    return t==gs&&!__atomic_load_n(&domain.gs_joined,__ATOMIC_ACQUIRE)&&p>=domain.gs_start&&p+n<=domain.gs_end;
}
int ps2_code_domains_scope_allowed(uintptr_t p,size_t n){
    const int ok=belongs(domain.api.code.current?domain.api.code.current():0,p,n);
    if(!ok)__atomic_store_n(&domain.last_error,PS2_HBXL_CODE_INVALID,__ATOMIC_RELEASE);
    return ok;
}
int ps2_code_domains_writer_wait(void){
    const uint64_t t=domain.api.code.current();
    if(!domain.initialized || (t!=domain.ee && t!=__atomic_load_n(&domain.gs,__ATOMIC_ACQUIRE)))return 1;
    __atomic_add_fetch(&domain.writer_waits,1,__ATOMIC_RELAXED);domain.api.sleep_ns(100000);return 0;
}
static int enter(void*unused){
    (void)unused;
    if(!domain.initialized||domain.publishing||self(0)!=domain.ee||(__atomic_load_n(&domain.gs,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&domain.gs_joined,__ATOMIC_ACQUIRE)))return 1;
    domain.publishing=1;return 0;
}
static int enter_range(void*unused,uintptr_t p,size_t n){
    (void)unused;
    if(domain.publishing || (p&4095)||(n&4095)||!belongs(self(0),p,n))return 1;
    domain.publishing=1;
    if(self(0)==domain.ee)domain.ee_publications++;else domain.gs_publications++;
    return 0;
}
static void leave(void*unused){(void)unused;domain.publishing=0;}
uint32_t ps2_hbxl_code_domains_bind(const Ps2HbxlCodeDomainsConfig*c,const Ps2HbxlCodeDomainsApi*a){
    if(domain.initialized||domain.arena.poisoned||ps2_code_host_bound())return PS2_HBXL_CODE_BUSY;
    if(!c||!a||!a->code.query||!a->code.send||!a->code.current||!a->code.clean||!a->code.invalidate||!a->sleep_ns)return PS2_HBXL_CODE_INVALID;
    const Ps2HbxlCodeConfig*b=&c->base;
    if(b->magic!=PS2_HBXL_CODE_MAGIC||b->version!=2||b->size!=sizeof(*c)||b->flags!=(PS2_HBXL_CODE_DOMAINS_FLAG|PS2_HBXL_CODE_EXCLUSIVE_RO)||b->reserved||!b->pid||!b->ro_pid||b->pid==b->ro_pid||!b->service||b->send!=(uintptr_t)a->code.send||b->bytes>PS2_MAX_PAGES*(uint64_t)PS2_PAGE||!range(b->source,b->bytes)||!range(b->shadow,b->bytes)||!range(b->rx,b->bytes)||!c->gs_offset||(c->gs_offset&4095)||!c->gs_bytes||(c->gs_bytes&4095)||c->gs_offset>=b->bytes||c->gs_bytes!=b->bytes-c->gs_offset)return PS2_HBXL_CODE_INVALID;
    if(overlaps(b->source,b->bytes,b->shadow,b->bytes)||overlaps(b->source,b->bytes,b->rx,b->bytes)||overlaps(b->shadow,b->bytes,b->rx,b->bytes))return PS2_HBXL_CODE_INVALID;
    for(unsigned i=0;i<2;i++){uint64_t p=i?b->shadow:b->source;
        if(overlaps(p,b->bytes,(uintptr_t)&domain,sizeof(domain))||overlaps(p,b->bytes,(uintptr_t)c,sizeof(*c))||overlaps(p,b->bytes,(uintptr_t)a,sizeof(*a)))return PS2_HBXL_CODE_INVALID;
    }
    uint64_t owner=a->code.current();if(!owner)return PS2_HBXL_CODE_INVALID;
    if(!covers(&a->code,b->source,b->bytes,0)||!covers(&a->code,b->shadow,b->bytes,0)||!covers(&a->code,b->rx,b->bytes,1))return PS2_HBXL_CODE_RANGE;
    memset(&domain,0,sizeof(domain));domain.api=*a;domain.ee=owner;domain.gs_start=b->rx+c->gs_offset;domain.gs_end=domain.gs_start+c->gs_bytes;
    domain.ro=(Ps2RoBackend){.pid=b->pid,.ro_pid=b->ro_pid,.service=b->service,.send=a->code.send,.source=b->source,.rx=b->rx,.bytes=b->bytes};
    if(ps2_arena_init(&domain.arena,(void*)(uintptr_t)b->source,(void*)(uintptr_t)b->shadow,(void*)(uintptr_t)b->rx,b->bytes,&domain.ro,ps2_ro_map_page,ps2_ro_unmap_page,a->code.clean,a->code.invalidate))return PS2_HBXL_CODE_INVALID;
    const Ps2CodeHostApi host={&domain,self,enter,leave,enter_range};domain.initialized=1;
    if(ps2_code_host_bind(&domain.arena,&host)){domain.initialized=0;return PS2_HBXL_CODE_BUSY;}return 0;
}
uint32_t ps2_hbxl_code_domains_register_gs(void){
    if(!domain.initialized||domain.api.code.current()==domain.ee||__atomic_load_n(&domain.gs_joined,__ATOMIC_ACQUIRE))return PS2_HBXL_CODE_INVALID;
    const uint64_t t=domain.api.code.current();if(!t)return PS2_HBXL_CODE_INVALID;
    uint64_t zero=0;
    if(!__atomic_compare_exchange_n(&domain.gs,&zero,t,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)&&zero!=t)return PS2_HBXL_CODE_BUSY;
    return 0;
}
uint32_t ps2_hbxl_code_domains_gs_joined(void){
    if(!domain.initialized||domain.api.code.current()!=domain.ee||domain.publishing)return PS2_HBXL_CODE_BUSY;
    __atomic_store_n(&domain.gs_joined,1,__ATOMIC_RELEASE);return 0;
}
uint32_t ps2_hbxl_code_domains_shutdown(void){
    if(!domain.initialized||domain.api.code.current()!=domain.ee||domain.publishing||domain.arena.poisoned||domain.arena.live_pages||domain.arena.mapped_pages||domain.arena.maps!=domain.arena.unmaps||(__atomic_load_n(&domain.gs,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&domain.gs_joined,__ATOMIC_ACQUIRE)))return PS2_HBXL_CODE_BUSY;
    if(ps2_code_host_bind(0,0))return PS2_HBXL_CODE_BUSY;domain.initialized=0;return 0;
}
void ps2_hbxl_code_domains_statistics(Ps2HbxlCodeDomainsStatistics*s){
    if(!s)return;
    *s=(Ps2HbxlCodeDomainsStatistics){.code={(uintptr_t)domain.arena.source,(uintptr_t)domain.arena.shadow,(uintptr_t)domain.arena.rx,domain.arena.bytes,domain.ee,domain.arena.maps,domain.arena.unmaps,domain.arena.publications,domain.initialized,domain.arena.poisoned,domain.arena.live_pages,domain.arena.mapped_pages,domain.arena.peak_pages,domain.arena.last_error,domain.ro.transport,domain.ro.service_result},.gs_owner=__atomic_load_n(&domain.gs,__ATOMIC_ACQUIRE),.gs_offset=domain.gs_start-(uintptr_t)domain.arena.rx,.gs_bytes=domain.gs_end-domain.gs_start,.ee_publications=domain.ee_publications,.gs_publications=domain.gs_publications,.writer_waits=__atomic_load_n(&domain.writer_waits,__ATOMIC_RELAXED),.gs_joined=__atomic_load_n(&domain.gs_joined,__ATOMIC_ACQUIRE),.last_domain_error=__atomic_load_n(&domain.last_error,__ATOMIC_ACQUIRE)};
}
