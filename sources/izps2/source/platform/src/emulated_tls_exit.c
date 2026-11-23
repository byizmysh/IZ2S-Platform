/* Alternate exit profile reusing the stable descriptor/storage implementation.
 * The original runtime remains separately buildable and unchanged. Only slot
 * binding, destructor registration and final detach are replaced here. */
#include "emulated_tls_exit.h"
#define ps2_tls_initialize __attribute__((visibility("hidden"))) ps2_tls_base_initialize
#define ps2_tls_detach_current __attribute__((visibility("hidden"))) ps2_tls_base_detach_current
#define __cxa_thread_atexit __attribute__((visibility("hidden"))) ps2_tls_base_thread_atexit
#include "emulated_tls.c"
#undef ps2_tls_initialize
#undef ps2_tls_detach_current
#undef __cxa_thread_atexit

enum { TLS_LIVE=0,TLS_DESTRUCTING=1,TLS_DESTRUCTED=2 };
static int destructors(Thread *t){
    if(t->exiting==TLS_DESTRUCTED)return 0;
    if(t->exiting==TLS_DESTRUCTING)return 1;
    t->exiting=TLS_DESTRUCTING;
    uint32_t iterations=0;
    while(t->count){
        if(++iterations>PS2_TLS_DTORS*16u)fatal(5);
        Destructor d=t->destructors[--t->count];d.function(d.argument);
        lock();statistics.destructors++;unlock();
    }
    t->exiting=TLS_DESTRUCTED;return 0;
}
static void lifecycle_thread_exit(uintptr_t pointer){
    Thread *t=(void*)pointer;if(!t)return;
    if(!statistics.ready || t->magic!=TLS_MAGIC || t->exiting==TLS_DESTRUCTING)fatal(4);
    api.set_value(statistics.slot,pointer);
    if(destructors(t))fatal(4);
    api.set_value(statistics.slot,0);
    for(uint32_t i=0;i<PS2_TLS_CONTROLS;i++)if(t->values[i]){
        api.release(t->values[i]);lock();statistics.releases++;unlock();
    }
    t->magic=0;api.release(t);
    lock();statistics.releases++;statistics.threads--;unlock();
}
int ps2_tls_initialize(const Ps2TlsApi *value){
    if(statistics.ready || !value || !value->allocate_slot || !value->free_slot || !value->get_value ||
       !value->set_value || !value->allocate || !value->release || !value->fatal)return 1;
    Ps2TlsApi copy=*value;uint32_t slot=UINT32_MAX;
    uint32_t result=copy.allocate_slot(&slot,lifecycle_thread_exit);if(result)return 2;
    api=copy;memset(&statistics,0,sizeof(statistics));memset(controls,0,sizeof(controls));
    statistics.slot=slot;statistics.ready=1;return 0;
}
int __cxa_thread_atexit(void (*function)(void*),void *argument,void *dso){
    (void)dso;Thread *t=current();
    if(!function || t->exiting==TLS_DESTRUCTED || t->count>=PS2_TLS_DTORS)return -1;
    t->destructors[t->count++]=(Destructor){function,argument};return 0;
}
int ps2_tls_run_current_destructors(void){
    if(!statistics.ready)return 1;
    Thread *t=(void*)api.get_value(statistics.slot);if(!t)return 0;
    if(t->magic!=TLS_MAGIC)fatal(2);return destructors(t);
}
void ps2_tls_detach_current(void){
    if(!statistics.ready)return;
    uintptr_t pointer=api.get_value(statistics.slot);if(pointer)lifecycle_thread_exit(pointer);
}
