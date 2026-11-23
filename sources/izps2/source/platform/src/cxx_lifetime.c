#include "cxx_lifetime.h"
#include <string.h>
typedef struct Entry {
    struct Entry *next;
    union { void (*argument)(void *);void (*plain)(void); } function;
    void *argument,*dso;
    uint32_t plain;
} Entry;
static Ps2CxxLifetimeApi api;
static Ps2CxxLifetimeStatistics statistics;
static Entry *head;
static uint32_t ready;
static uint64_t finalizer_owner,finalizer_depth;
__attribute__((noreturn)) static void fatal(uint32_t code){
    if(api.fatal)api.fatal(code);__builtin_trap();
}
static int available(void){return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=0;}
int ps2_cxx_lifetime_initialize(const Ps2CxxLifetimeApi *value){
    if(available() || !value || !value->lock || !value->unlock || !value->allocate || !value->release || !value->fatal || !value->self || !value->wait || !value->wake_all)return 1;
    Ps2CxxLifetimeApi copy=*value;api=copy;head=0;memset(&statistics,0,sizeof(statistics));
    finalizer_owner=finalizer_depth=0;
    __atomic_store_n(&ready,1,__ATOMIC_RELEASE);return 0;
}
static int insert(void (*argument)(void*),void (*plain)(void),void *data,void *dso){
    if(!available() || (!argument && !plain))return -1;
    Entry *entry=api.allocate(sizeof(*entry),_Alignof(Entry));
    if(!entry)return -1;
    if((uintptr_t)entry%_Alignof(Entry))fatal(1);
    entry->plain=plain!=0;if(plain)entry->function.plain=plain;else entry->function.argument=argument;
    entry->argument=data;entry->dso=dso;
    api.lock();entry->next=head;head=entry;statistics.registered++;statistics.pending++;statistics.allocated++;api.unlock();return 0;
}
int __cxa_atexit(void (*function)(void *),void *argument,void *dso){return insert(function,0,argument,dso);}
int atexit(void (*function)(void)){return insert(0,function,0,0);}
void __cxa_finalize(void *dso){
    if(!available())return;
    uint64_t self=api.self();if(!self)fatal(2);
    api.lock();
    while(finalizer_owner && finalizer_owner!=self)api.wait();
    finalizer_owner=self;finalizer_depth++;
    api.unlock();
    for(;;){
        api.lock();Entry **link=&head;
        while(*link && dso && (*link)->dso!=dso)link=&(*link)->next;
        Entry *entry=*link;
        if(!entry){
            if(!--finalizer_depth){finalizer_owner=0;api.wake_all();}
            api.unlock();return;
        }
        *link=entry->next;statistics.pending--;statistics.inflight++;statistics.invoked++;api.unlock();
        /* No registry lock is held across user code. The removed entry remains
         * owned by this invocation until the callback returns. */
        if(entry->plain)entry->function.plain();else entry->function.argument(entry->argument);
        api.release(entry);
        api.lock();statistics.inflight--;statistics.released++;api.unlock();
    }
}
void ps2_cxx_lifetime_statistics(Ps2CxxLifetimeStatistics *out){
    if(!out)return;if(!available()){*out=statistics;return;}
    api.lock();*out=statistics;api.unlock();
}
int ps2_cxx_lifetime_shutdown(void){
    if(!available())return 1;
    api.lock();int busy=head!=0 || statistics.inflight!=0 || finalizer_depth!=0;
    if(!busy)__atomic_store_n(&ready,0,__ATOMIC_RELEASE);api.unlock();return busy?2:0;
}
