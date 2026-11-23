#include "once.h"
static Ps2OnceApi api;
static Ps2OnceStatistics statistics;
static uint32_t ready;
[[noreturn]] static void fatal(uint32_t code) {
    if(api.fatal)api.fatal(code);
    __builtin_trap();
}
static void add(uint64_t &value) { __atomic_fetch_add(&value,1,__ATOMIC_RELAXED); }
int ps2_once_initialize(const Ps2OnceApi *value) {
    if(__atomic_load_n(&ready,__ATOMIC_ACQUIRE) || !value || !value->wait || !value->wake_all || !value->fatal)return 1;
    api=*value;__atomic_store_n(&ready,1,__ATOMIC_RELEASE);return 0;
}
struct Execution {
    uint32_t *control;
    bool completed=false;
    ~Execution() {
        __atomic_store_n(control,completed?2u:0u,__ATOMIC_RELEASE);
        if(completed)add(statistics.completed);else add(statistics.rolled_back);
        api.wake_all(control);
    }
};
int ps2_once_run(uint32_t *control,void (*initialize)(void)) {
    if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE) || !control || (reinterpret_cast<uintptr_t>(control)&3) || !initialize)fatal(1);
    for(;;) {
        uint32_t state=__atomic_load_n(control,__ATOMIC_ACQUIRE);
        if(state==2)return 0;
        if(state==0) {
            uint32_t expected=0;
            if(!__atomic_compare_exchange_n(control,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
            Execution execution{control};add(statistics.executions);
            initialize();execution.completed=true;return 0;
        }
        if(state!=1)fatal(2);
        add(statistics.waits);api.wait(control,1);
    }
}
void ps2_once_statistics(Ps2OnceStatistics *out) {
    out->executions=__atomic_load_n(&statistics.executions,__ATOMIC_RELAXED);
    out->completed=__atomic_load_n(&statistics.completed,__ATOMIC_RELAXED);
    out->rolled_back=__atomic_load_n(&statistics.rolled_back,__ATOMIC_RELAXED);
    out->waits=__atomic_load_n(&statistics.waits,__ATOMIC_RELAXED);
}
