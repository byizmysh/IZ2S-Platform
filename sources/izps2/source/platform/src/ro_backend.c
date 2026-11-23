#include "ro_backend.h"
#include <string.h>
static int request(Ps2RoBackend *r,uint32_t command,uintptr_t source,uintptr_t rx) {
    if(!r || !r->send || !r->service || !r->pid || !r->ro_pid || !r->bytes ||
       (r->bytes&(PS2_PAGE-1)) || r->bytes>PS2_MAX_PAGES*(size_t)PS2_PAGE ||
       r->source>UINTPTR_MAX-r->bytes || r->rx>UINTPTR_MAX-r->bytes ||
       source<r->source || rx<r->rx || source-r->source!=rx-r->rx ||
       source-r->source>=r->bytes || ((source|rx)&(PS2_PAGE-1))) return 1;
    _Alignas(4096) uint8_t message[4096];
    memset(message,0,sizeof(message));
    uint32_t *p=(void*)message;
    p[0]=4; p[1]=0x80000000u+16; p[2]=3; p[5]=0xffff8001u;
    p[8]=0x49434653; p[10]=command;
    uint64_t *in=(void*)(message+48);
    in[0]=r->pid; in[1]=source; in[2]=rx; in[3]=PS2_PAGE;
    r->transport=r->send(message,sizeof(message),r->service);
    if(r->transport)return 2;
    if(!(p[1]&0x80000000u) || p[2]!=1 || p[8]!=0x4f434653)return 3;
    if(((uint64_t)p[3]|((uint64_t)p[4]<<32))!=r->ro_pid)return 4;
    r->service_result=p[10];
    return r->service_result?5:0;
}
int ps2_ro_map_page(void *context,uintptr_t source,uintptr_t rx) { return request(context,0x524f0012,source,rx); }
int ps2_ro_unmap_page(void *context,uintptr_t source,uintptr_t rx) { return request(context,0x524f0013,source,rx); }
void ps2_ro_clean(uintptr_t address,size_t bytes) {
#if defined(__aarch64__)
    for(size_t i=0;i<bytes;i+=4)__asm__ volatile("dc cvau, %0"::"r"(address+i):"memory");
    __asm__ volatile("dsb ish":::"memory");
#else
    (void)address; (void)bytes;
#endif
}
void ps2_ro_invalidate(uintptr_t address,size_t bytes) {
#if defined(__aarch64__)
    for(size_t i=0;i<bytes;i+=4)__asm__ volatile("ic ivau, %0"::"r"(address+i):"memory");
    __asm__ volatile("dsb ish; isb":::"memory");
#else
    (void)address; (void)bytes;
#endif
}
