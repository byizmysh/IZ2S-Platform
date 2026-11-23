#include "code_arena.h"
#include <string.h>
#include <limits.h>

static int valid_range(uintptr_t p, size_t bytes) { return p && bytes <= UINTPTR_MAX - p; }
static int overlap(uintptr_t a, uintptr_t b, size_t n) { return a < b+n && b < a+n; }
static int fail(Ps2CodeArena *a, int error) {
    a->poisoned=1; a->last_error=(uint32_t)error; return error;
}
int ps2_arena_init(Ps2CodeArena *a, void *source, void *shadow, void *rx, size_t bytes,
                   void *opaque, Ps2PageOperation map, Ps2PageOperation unmap,
                   Ps2CacheOperation clean, Ps2CacheOperation invalidate) {
    uintptr_t s=(uintptr_t)source,w=(uintptr_t)shadow,x=(uintptr_t)rx;
    if(!a || !bytes || bytes/PS2_PAGE>PS2_MAX_PAGES ||
       ((s|w|x|bytes)&(PS2_PAGE-1)) || !valid_range(s,bytes) ||
       !valid_range(w,bytes) || !valid_range(x,bytes) ||
       overlap(s,w,bytes) || overlap(s,x,bytes) || overlap(w,x,bytes) ||
       !map || !unmap || !clean || !invalidate) return 1;
    memset(a,0,sizeof(*a)); a->source=source; a->shadow=shadow; a->rx=rx;
    a->bytes=bytes; a->pages=(uint32_t)(bytes/PS2_PAGE); a->opaque=opaque;
    a->map=map; a->unmap=unmap; a->clean=clean; a->invalidate=invalidate;
    return 0;
}
static int find_run(Ps2CodeArena *a, const void *rx, size_t size, uint32_t *first, uint32_t *count) {
    uintptr_t x=(uintptr_t)rx, base=(uintptr_t)a->rx;
    if(a->poisoned || !size || x<base || x-base>=a->bytes || ((x-base)&(PS2_PAGE-1))) return 1;
    size_t needed=(size-1)/PS2_PAGE+1;
    uint32_t i=(uint32_t)((x-base)/PS2_PAGE), n=a->runs[i];
    if(!n || n==UINT32_MAX || n!=needed || n>a->pages-i) return 1;
    *first=i; *count=n; return 0;
}
static int allocate(Ps2CodeArena *a, const void *code, size_t size, void **out,int lazy) {
    if(out) *out=0;
    if(!a || !out || a->poisoned || !size || size>a->bytes) return 1;
    uint32_t n=(uint32_t)((size-1)/PS2_PAGE+1), found=0, first=0;
    for(uint32_t i=0;i<a->pages;i++) {
        if(a->runs[i]) found=0;
        else { if(!found)first=i; if(++found==n)break; }
    }
    if(found!=n)return 2;
    a->runs[first]=n;
    for(uint32_t i=1;i<n;i++)a->runs[first+i]=UINT32_MAX;
    for(uint32_t i=0;i<n;i++)a->heads[first+i]=first+1;
    a->live_pages+=n;
    if(a->live_pages>a->peak_pages)a->peak_pages=a->live_pages;
    size_t offset=(size_t)first*PS2_PAGE;
    memset(a->shadow+offset,0,(size_t)n*PS2_PAGE);
    if(code)memcpy(a->shadow+offset,code,size);
    if(lazy){a->allocations++;*out=a->rx+offset;return 0;}
    memcpy(a->source+offset,a->shadow+offset,(size_t)n*PS2_PAGE);
    for(uint32_t i=0;i<n;i++) {
        size_t at=offset+(size_t)i*PS2_PAGE;
        a->clean((uintptr_t)(a->source+at),PS2_PAGE);
        int r=a->map(a->opaque,(uintptr_t)(a->source+at),(uintptr_t)(a->rx+at));
        if(r)return fail(a,100+r);
        a->maps++;a->mapped[first+i]=1;a->mapped_pages++;a->invalidate((uintptr_t)(a->rx+at),PS2_PAGE);
        if(memcmp(a->rx+at,a->shadow+at,PS2_PAGE))return fail(a,190);
    }
    a->allocations++; *out=a->rx+offset; return 0;
}
int ps2_arena_allocate(Ps2CodeArena *a,const void *code,size_t size,void **out) {
    if(!code) { if(out)*out=0; return 1; }
    return allocate(a,code,size,out,0);
}
int ps2_arena_reserve_zero(Ps2CodeArena *a,size_t size,void **out) { return allocate(a,0,size,out,0); }
int ps2_arena_reserve_lazy(Ps2CodeArena *a,size_t size,void **out) { return allocate(a,0,size,out,1); }
void *ps2_arena_writable(Ps2CodeArena *a, const void *rx, size_t size) {
    uint32_t first,n;
    if(!a || find_run(a,rx,size,&first,&n))return 0;
    return a->shadow+(size_t)first*PS2_PAGE;
}
static int publish(Ps2CodeArena *a,uint32_t first,uint32_t n) {
    for(uint32_t i=first;i<first+n;i++) {
        size_t at=(size_t)i*PS2_PAGE;
        if(a->mapped[i] && !memcmp(a->rx+at,a->shadow+at,PS2_PAGE))continue;
        int r;
        if(a->mapped[i]) {
            r=a->unmap(a->opaque,(uintptr_t)(a->source+at),(uintptr_t)(a->rx+at));
            if(r)return fail(a,200+r);
            a->unmaps++;a->mapped[i]=0;a->mapped_pages--;
        }
        memcpy(a->source+at,a->shadow+at,PS2_PAGE);
        a->clean((uintptr_t)(a->source+at),PS2_PAGE);
        r=a->map(a->opaque,(uintptr_t)(a->source+at),(uintptr_t)(a->rx+at));
        if(r)return fail(a,300+r);
        a->maps++;a->mapped[i]=1;a->mapped_pages++;a->invalidate((uintptr_t)(a->rx+at),PS2_PAGE);
        if(memcmp(a->rx+at,a->shadow+at,PS2_PAGE))return fail(a,390);
        a->publications++;
    }
    return 0;
}
int ps2_arena_publish(Ps2CodeArena *a,void *rx,size_t size) {
    uint32_t first,n;
    if(!a || find_run(a,rx,size,&first,&n))return 1;
    return publish(a,first,n);
}
static int find_range(Ps2CodeArena *a,const void *rx,size_t size,uint32_t *first,uint32_t *n) {
    if(!a || a->poisoned || !size)return 1;
    uintptr_t p=(uintptr_t)rx,base=(uintptr_t)a->rx;
    if(p<base || p-base>=a->bytes || size>a->bytes-(p-base))return 1;
    size_t offset=p-base;
    uint32_t page=(uint32_t)(offset/PS2_PAGE),head=a->heads[page];
    if(!head || head-1>page)return 1;
    head--;
    uint32_t count=a->runs[head];
    if(!count || count==UINT32_MAX || count>a->pages-head || offset+size>(size_t)(head+count)*PS2_PAGE)return 1;
    *first=(uint32_t)(offset/PS2_PAGE);
    *n=(uint32_t)((offset+size-1)/PS2_PAGE+1-*first);
    return 0;
}
void *ps2_arena_writable_range(Ps2CodeArena *a,const void *rx,size_t size) {
    uint32_t first,n;
    if(find_range(a,rx,size,&first,&n))return 0;
    return a->shadow+((uintptr_t)rx-(uintptr_t)a->rx);
}
int ps2_arena_publish_range(Ps2CodeArena *a,void *rx,size_t size) {
    uint32_t first,n;
    if(find_range(a,rx,size,&first,&n))return 1;
    return publish(a,first,n);
}
int ps2_arena_release(Ps2CodeArena *a, void *rx, size_t size) {
    uint32_t first,n;
    if(!a || find_run(a,rx,size,&first,&n))return 1;
    for(uint32_t i=first;i<first+n;i++) {
        if(!a->mapped[i])continue;
        size_t at=(size_t)i*PS2_PAGE;
        int r=a->unmap(a->opaque,(uintptr_t)(a->source+at),(uintptr_t)(a->rx+at));
        if(r)return fail(a,400+r);
        a->unmaps++;a->mapped[i]=0;a->mapped_pages--;
    }
    for(uint32_t i=first;i<first+n;i++){a->runs[i]=0;a->heads[i]=0;}
    a->live_pages-=n; a->releases++; return 0;
}
