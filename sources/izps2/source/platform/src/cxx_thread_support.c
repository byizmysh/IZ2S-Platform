#include "cxx_thread_support.h"
#include <errno.h>
#include <stddef.h>
#include <time.h>
static Ps2CxxThreadApi sdk;
static Ps2CxxThreadStatistics statistics;
static uint32_t initialized;
_Static_assert(sizeof(struct timespec)==sizeof(Ps2SdkTimespec),"SDK timespec capacity");
_Static_assert(offsetof(struct timespec,tv_sec)==offsetof(Ps2SdkTimespec,seconds),"SDK seconds offset");
_Static_assert(offsetof(struct timespec,tv_nsec)==offsetof(Ps2SdkTimespec,nanoseconds),"SDK nanoseconds offset");
static void add(uint64_t *p){__atomic_fetch_add(p,1,__ATOMIC_RELAXED);}
int ps2_cxx_thread_initialize(const Ps2CxxThreadApi *imports){
    if(initialized || !imports || !imports->available_core_mask || !imports->nanosleep || !imports->errno_location)return EINVAL;
    Ps2CxxThreadApi copy=*imports;
    uint64_t mask=copy.available_core_mask();
    if(!mask || !copy.errno_location())return ENODEV;
    sdk=copy;statistics.initial_core_mask=mask;initialized=1;return 0;
}
/* These entry points require initialization. A violated owner contract traps;
 * no process-global errno or fabricated CPU count replaces a missing SDK. */
static void require_ready(void){if(!initialized)__builtin_trap();}
int get_nprocs(void){
    require_ready();add(&statistics.core_queries);
    uint64_t mask=sdk.available_core_mask();unsigned count=0;
    while(mask){mask&=mask-1;count++;}
    return (int)count;
}
int nanosleep(const struct timespec *request,struct timespec *remaining){
    require_ready();add(&statistics.sleep_calls);
    /* Forward aliasing and error/remainder behavior unchanged. In particular,
     * libstdc++ calls nanosleep(&ts,&ts) and retries only with SDK errno EINTR. */
    return sdk.nanosleep((const Ps2SdkTimespec *)request,(Ps2SdkTimespec *)remaining);
}
int *__errno_location(void){
    require_ready();add(&statistics.errno_queries);
    int *p=sdk.errno_location();if(!p)__builtin_trap();return p;
}
void ps2_cxx_thread_statistics(Ps2CxxThreadStatistics *out){
    if(!out)return;
    out->initial_core_mask=statistics.initial_core_mask;
    out->core_queries=__atomic_load_n(&statistics.core_queries,__ATOMIC_RELAXED);
    out->sleep_calls=__atomic_load_n(&statistics.sleep_calls,__ATOMIC_RELAXED);
    out->errno_queries=__atomic_load_n(&statistics.errno_queries,__ATOMIC_RELAXED);
}
