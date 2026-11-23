#include "emulated_tls.h"
#include <string.h>
#include <limits.h>
typedef struct { void (*function)(void *);void *argument; } Destructor;
typedef struct {
    uint64_t magic;
    void *values[PS2_TLS_CONTROLS];
    Destructor destructors[PS2_TLS_DTORS];
    uint32_t count,exiting;
} Thread;
_Static_assert(sizeof(Ps2TlsControl)==4*sizeof(uintptr_t),"emulated TLS ABI");
#define TLS_MAGIC UINT64_C(0x505332544c533031)
static Ps2TlsApi api;
static Ps2TlsStatistics statistics;
static Ps2TlsControl *controls[PS2_TLS_CONTROLS];
static uint32_t locked;
static void lock(void) {
    while(__atomic_exchange_n(&locked,1,__ATOMIC_ACQUIRE)) {
#if defined(__aarch64__)
        __asm__ volatile("yield":::"memory");
#endif
    }
}
static void unlock(void) { __atomic_store_n(&locked,0,__ATOMIC_RELEASE); }
__attribute__((noreturn)) static void fatal(uint32_t code) {
    statistics.last_error=code;
    if(api.fatal)api.fatal(code);
    __builtin_trap();
}
static Thread *current(void) {
    if(!statistics.ready)fatal(1);
    Thread *t=(void*)api.get_value(statistics.slot);
    if(t) { if(t->magic!=TLS_MAGIC)fatal(2);return t; }
    t=api.allocate(sizeof(*t),16);
    if(!t || ((uintptr_t)t&15))fatal(3);
    memset(t,0,sizeof(*t));t->magic=TLS_MAGIC;
    api.set_value(statistics.slot,(uintptr_t)t);
    lock();statistics.threads++;statistics.allocations++;unlock();
    return t;
}
static void thread_exit(uintptr_t pointer) {
    Thread *t=(void*)pointer;
    if(!t)return;
    if(!statistics.ready || t->magic!=TLS_MAGIC || t->exiting)fatal(4);
    /* Some slot APIs clear the key before invoking its destructor. TLS
     * destructors still need the original thread's values while they run. */
    api.set_value(statistics.slot,pointer);t->exiting=1;
    uint32_t iterations=0;
    while(t->count) {
        if(++iterations>PS2_TLS_DTORS*16u)fatal(5);
        Destructor d=t->destructors[--t->count];d.function(d.argument);
        lock();statistics.destructors++;unlock();
    }
    api.set_value(statistics.slot,0);
    for(uint32_t i=0;i<PS2_TLS_CONTROLS;i++)if(t->values[i]) {
        api.release(t->values[i]);lock();statistics.releases++;unlock();
    }
    t->magic=0;api.release(t);
    lock();statistics.releases++;statistics.threads--;unlock();
}
int ps2_tls_initialize(const Ps2TlsApi *a) {
    if(statistics.ready || !a || !a->allocate_slot || !a->free_slot || !a->get_value ||
       !a->set_value || !a->allocate || !a->release || !a->fatal)return 1;
    uint32_t slot=UINT32_MAX;
    uint32_t result=a->allocate_slot(&slot,thread_exit);
    if(result)return 2;
    api=*a;memset(&statistics,0,sizeof(statistics));memset(controls,0,sizeof(controls));
    statistics.slot=slot;statistics.ready=1;return 0;
}
void *__emutls_get_address(Ps2TlsControl *c) {
    if(!c || !c->size || c->size>16u*1024u*1024u || !c->alignment ||
       (c->alignment&(c->alignment-1)) || c->alignment>1024u*1024u)fatal(6);
    Thread *t=current();
    lock();
    uintptr_t index=c->object.index;
    if(!index) {
        if(statistics.controls>=PS2_TLS_CONTROLS){unlock();fatal(7);}
        index=++statistics.controls;controls[index-1]=c;c->object.index=index;
    }
    if(index>statistics.controls || controls[index-1]!=c){unlock();fatal(8);}
    unlock();
    void *value=t->values[index-1];
    if(!value) {
        size_t alignment=c->alignment<sizeof(void*)?sizeof(void*):c->alignment;
        value=api.allocate(c->size,alignment);
        if(!value || ((uintptr_t)value&(alignment-1)))fatal(9);
        if(c->initial_value)memcpy(value,c->initial_value,c->size);else memset(value,0,c->size);
        t->values[index-1]=value;lock();statistics.allocations++;unlock();
    }
    return value;
}
int __cxa_thread_atexit(void (*function)(void *),void *argument,void *dso) {
    (void)dso;Thread *t=current();
    if(!function || t->count>=PS2_TLS_DTORS)return -1;
    t->destructors[t->count++]=(Destructor){function,argument};return 0;
}
void ps2_tls_detach_current(void) {
    if(!statistics.ready)return;
    uintptr_t t=api.get_value(statistics.slot);if(t)thread_exit(t);
}
int ps2_tls_shutdown(void) {
    lock();
    if(!statistics.ready || statistics.threads){unlock();return 1;}
    for(uint32_t i=0;i<statistics.controls;i++)controls[i]->object.index=0;
    uint32_t slot=statistics.slot;statistics.ready=0;unlock();
    api.free_slot(slot);return 0;
}
void ps2_tls_statistics(Ps2TlsStatistics *s) { lock();*s=statistics;unlock(); }
