#include "hbxl_code_domains.h"
#include <atomic>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// Exact original call sites: MTGS ThreadEntryPoint sets name before GSopen;
// VMManager CPUThreadShutdown calls ShutdownThread before SysMemory::Release.
// The real function performs the actual join. These wrappers do not emulate it.
static std::atomic<uint64_t> gs_pthread{0};
static std::atomic<bool> gs_join_confirmed{false};
// The EE is the sole legitimate MTGS joiner. Filtering by its identity also
// prevents a delayed unrelated join on a recycled pthread_t from certifying
// the GS of a later bind generation. The underlying join result is untouched.
extern "C" int ps2_hbxl_code_domains_reusable_reset_ready(void);
extern "C" int ps2_hbxl_code_domains_reusable_join_owner(void);
static std::atomic_flag observer_gate=ATOMIC_FLAG_INIT;
static void lock_observer(){while(observer_gate.test_and_set(std::memory_order_acquire)){}}
static void unlock_observer(){observer_gate.clear(std::memory_order_release);}
extern "C" void ps2_hbxl_code_domains_observe_join(uint64_t thread,int result){
    if(!ps2_hbxl_code_domains_reusable_join_owner())return;
    lock_observer();
    if(thread==gs_pthread.load(std::memory_order_acquire))
        gs_join_confirmed.store(result==0,std::memory_order_release);
    unlock_observer();
}
// Called weakly by the v5 binder only after full arena quiescence. Direct
// misuse also fails closed; successful musl join is not SDK thread reap.
extern "C" int ps2_hbxl_code_domains_reset_observer(void){
    if(!ps2_hbxl_code_domains_reusable_reset_ready())return PS2_HBXL_CODE_BUSY;
    lock_observer();
    if(gs_pthread.load(std::memory_order_acquire)&&!gs_join_confirmed.load(std::memory_order_acquire)){
        unlock_observer();return PS2_HBXL_CODE_BUSY;
    }
    gs_pthread.store(0,std::memory_order_release);
    gs_join_confirmed.store(false,std::memory_order_release);
    unlock_observer();return 0;
}
extern "C" void __real__ZN9Threading22SetNameOfCurrentThreadEPKc(const char*);
extern "C" void __wrap__ZN9Threading22SetNameOfCurrentThreadEPKc(const char*n){
    __real__ZN9Threading22SetNameOfCurrentThreadEPKc(n);
    if(n&&!std::strcmp(n,"GS")){
        const uint64_t raw=(uintptr_t)pthread_self();uint64_t zero=0;lock_observer();
        if(!raw||!gs_pthread.compare_exchange_strong(zero,raw)||ps2_hbxl_code_domains_register_gs()){
            std::fputs("PS2 code domains: GS registration failed\n",stderr);std::abort();
        }
        unlock_observer();
    }
}
extern "C" void __real__ZN4MTGS14ShutdownThreadEv();
extern "C" void __wrap__ZN4MTGS14ShutdownThreadEv(){
    __real__ZN4MTGS14ShutdownThreadEv();
    // LnxThreads::Join reports an assertion on pthread_join failure, but an
    // assertion handler is not itself a successful join receipt. Observe the
    // actual join result for this exact private pthread handle. Other device
    // workers may still exist here and cannot substitute for this receipt.
    if((gs_pthread.load(std::memory_order_acquire)&&!gs_join_confirmed.load(std::memory_order_acquire))||ps2_hbxl_code_domains_gs_joined()){
        std::fputs("PS2 code domains: actual GS join receipt rejected\n",stderr);std::abort();
    }
}
