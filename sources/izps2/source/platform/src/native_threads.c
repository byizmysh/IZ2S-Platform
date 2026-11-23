#include "native_threads.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
typedef struct {uint32_t mutex[2],condition[2],count,waiters;} Semaphore;
_Static_assert(sizeof(Semaphore)==24 && _Alignof(Semaphore)==4,"port semaphore capacity/alignment");
static Ps2SdkThreadApi sdk;
static Ps2NativeThreadStatistics statistics;
static uint32_t initialized;
static int error(int value){*sdk.sdk_errno_location()=value;return -1;}
static void add(uint64_t *p){__atomic_fetch_add(p,1,__ATOMIC_RELAXED);}
static int sem_init(void *p,int shared,unsigned value){
    if(shared)return error(ENOTSUP);
    if(value>INT_MAX)return error(EINVAL);
    Semaphore *s=p;memset(s,0,sizeof(*s));
    int result=sdk.mutex_init(s->mutex,0);if(result)return error(result);
    result=sdk.cond_init(s->condition,0);
    if(result){sdk.mutex_destroy(s->mutex);return error(result);}
    s->count=value;return 0;
}
static int sem_destroy(void *p){
    Semaphore *s=p;int result=sdk.mutex_lock(s->mutex);if(result)return error(result);
    unsigned waiters=s->waiters;result=sdk.mutex_unlock(s->mutex);
    if(result)return error(result);if(waiters)return error(EBUSY);
    result=sdk.cond_destroy(s->condition);if(!result)result=sdk.mutex_destroy(s->mutex);
    return result?error(result):0;
}
static int wait(void *p,const Ps2SdkTimespec *time,int try_only){
    Semaphore *s=p;int result=sdk.mutex_lock(s->mutex);if(result)return error(result);
    add(&statistics.semaphore_waits);
    while(!s->count){
        if(try_only){result=EAGAIN;break;}
        s->waiters++;add(&statistics.semaphore_blocks);
        result=time?sdk.cond_timedwait(s->condition,s->mutex,time):sdk.cond_wait(s->condition,s->mutex);
        s->waiters--;
        if(result && !s->count)break;
        result=0; // A token concurrent with the timeout may be consumed.
    }
    if(!result)s->count--;
    int released=sdk.mutex_unlock(s->mutex);
    // No object access after this unlock: a final waiter can destroy/reuse it.
    return released?error(released):result?error(result):0;
}
static int sem_wait(void *p){return wait(p,0,0);}
static int sem_trywait(void *p){return wait(p,0,1);}
static int sem_timedwait(void *p,const Ps2SdkTimespec *time){return wait(p,time,0);}
static int sem_post(void *p){
    Semaphore *s=p;int result=sdk.mutex_lock(s->mutex);if(result)return error(result);
    if(s->count==INT_MAX)result=EOVERFLOW;
    else{
        s->count++;add(&statistics.semaphore_posts);
        if(s->waiters)result=sdk.cond_signal(s->condition);
    }
    int released=sdk.mutex_unlock(s->mutex);
    // Publication is the final access to s, including on the success path.
    return released?error(released):result?error(result):0;
}
uint32_t ps2_native_threads_initialize(const Ps2SdkThreadApi *imports){
    if(initialized || !imports || !imports->sdk_errno_location)return EINVAL;
    Ps2SdkThreadApi profile=*imports;
    profile.thread_attr_bytes=56;profile.mutex_attr_bytes=4;profile.cond_attr_bytes=4;
    profile.mutex_bytes=8;profile.cond_bytes=8;profile.rwlock_bytes=48;profile.sem_bytes=sizeof(Semaphore);
    profile.stack_page=4096;profile.default_stack=2*1024*1024;
    profile.inline_sync_no_resources=1;profile.detached_code_resident=0;
    profile.sem_init=sem_init;profile.sem_destroy=sem_destroy;profile.sem_wait=sem_wait;
    profile.sem_trywait=sem_trywait;profile.sem_timedwait=sem_timedwait;profile.sem_post=sem_post;
    sdk=*imports;int result=ps2_sdk_threads_initialize(&profile);
    if(result){memset(&sdk,0,sizeof(sdk));return (uint32_t)result;}
    initialized=1;return 0;
}
void ps2_native_threads_statistics(Ps2NativeThreadStatistics *out){
    out->semaphore_waits=__atomic_load_n(&statistics.semaphore_waits,__ATOMIC_RELAXED);
    out->semaphore_blocks=__atomic_load_n(&statistics.semaphore_blocks,__ATOMIC_RELAXED);
    out->semaphore_posts=__atomic_load_n(&statistics.semaphore_posts,__ATOMIC_RELAXED);
}
