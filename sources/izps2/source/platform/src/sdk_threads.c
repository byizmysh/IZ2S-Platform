#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sdk_threads.h"
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#ifdef pthread_equal
#undef pthread_equal
#endif
#ifdef PS2_PTHREAD_WRAP
#define PUBLIC_I(name) __wrap_##name
#define PUBLIC(name) PUBLIC_I(name)
#else
#define PUBLIC(name) name
#endif
#define MAGIC UINT32_C(0x50530000)
#define ATTR_MAGIC UINT64_C(0x5053324154545231)
enum {MUTEX=1,COND=2,RWLOCK=3,SEM=4,INITIALIZING=1,READY=2,DEAD=3};
typedef struct {uint64_t magic;size_t stack;int detach;} Attr;
static Ps2SdkThreadApi api;
static Ps2SdkThreadStatistics stats;
static uint32_t ready;
_Static_assert(sizeof(pthread_t)==8 && sizeof(time_t)==8,"64-bit target ABI");
_Static_assert(sizeof(Attr)<=sizeof(pthread_attr_t),"port attribute capacity");
_Static_assert(_Alignof(pthread_mutex_t)>=4 && _Alignof(pthread_cond_t)>=4 && _Alignof(pthread_rwlock_t)>=4 && _Alignof(sem_t)>=4,"inline storage alignment");
static uint32_t tag(unsigned kind,unsigned state){return MAGIC|((uint32_t)kind<<8)|state;}
static void add(uint64_t *value){__atomic_fetch_add(value,1,__ATOMIC_RELAXED);}
static int available(void){return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=0;}
static int bound(size_t bytes,size_t object){return bytes && object>=8 && bytes<=object-8;}
static void use(void *p){__atomic_fetch_add((uint32_t*)p+1,1,__ATOMIC_RELAXED);}
static void unuse(void *p){__atomic_fetch_sub((uint32_t*)p+1,1,__ATOMIC_RELEASE);}
static int busy(void *p){return __atomic_load_n((uint32_t*)p+1,__ATOMIC_ACQUIRE)!=0;}
int ps2_sdk_threads_initialize(const Ps2SdkThreadApi *a){
    if(available() || !a || !a->inline_sync_no_resources || !a->sleep_ns ||
       !a->thread_attr_bytes || a->thread_attr_bytes>256 || !a->mutex_attr_bytes || a->mutex_attr_bytes>256 || !a->cond_attr_bytes || a->cond_attr_bytes>256 || !a->stack_page || (a->stack_page&(a->stack_page-1)) || a->stack_page>1024*1024 ||
       a->default_stack<PTHREAD_STACK_MIN || !bound(a->mutex_bytes,sizeof(pthread_mutex_t)) || !bound(a->cond_bytes,sizeof(pthread_cond_t)) ||
       !a->thread_attr_init || !a->thread_attr_destroy || !a->thread_attr_stack || !a->thread_attr_detach ||
       !a->thread_create || !a->thread_join || !a->thread_detach || !a->thread_self ||
       !a->mutex_attr_init || !a->mutex_attr_destroy || !a->mutex_attr_type || !a->mutex_init || !a->mutex_destroy || !a->mutex_lock || !a->mutex_trylock || !a->mutex_unlock ||
       !a->cond_attr_init || !a->cond_attr_destroy || !a->cond_attr_clock || !a->cond_init || !a->cond_destroy || !a->cond_wait || !a->cond_timedwait || !a->cond_signal || !a->cond_broadcast)return EINVAL;
    if(a->rwlock_bytes && (!bound(a->rwlock_bytes,sizeof(pthread_rwlock_t)) || !a->rwlock_init || !a->rwlock_destroy || !a->rwlock_read || !a->rwlock_write || !a->rwlock_unlock))return EINVAL;
    if(a->sem_bytes && (!bound(a->sem_bytes,sizeof(sem_t)) || !a->sem_init || !a->sem_destroy || !a->sem_wait || !a->sem_trywait || !a->sem_timedwait || !a->sem_post || !a->sdk_errno_location))return EINVAL;
    api=*a;__atomic_store_n(&ready,1,__ATOMIC_RELEASE);return 0;
}
static int init_storage(void *p,unsigned kind,unsigned value){
    _Alignas(16) unsigned char attributes[256]={0};void *data=(unsigned char*)p+8;int result;
    __atomic_store_n((uint32_t*)p+1,0,__ATOMIC_RELAXED);
    if(kind==MUTEX){
        memset(data,0,api.mutex_bytes);result=api.mutex_attr_init(attributes);if(result)return result;
        result=api.mutex_attr_type(attributes,(int)value);if(!result)result=api.mutex_init(data,attributes);
        int cleanup=api.mutex_attr_destroy(attributes);if(!result)result=cleanup;if(!result)add(&stats.mutex_initializations);return result;
    }
    if(kind==COND){
        memset(data,0,api.cond_bytes);result=api.cond_attr_init(attributes);if(result)return result;
        result=api.cond_attr_clock(attributes,(int)value);if(!result)result=api.cond_init(data,attributes);
        int cleanup=api.cond_attr_destroy(attributes);if(!result)result=cleanup;if(!result)add(&stats.cond_initializations);return result;
    }
    if(kind==RWLOCK){if(!api.rwlock_bytes)return ENOTSUP;memset(data,0,api.rwlock_bytes);result=api.rwlock_init(data,0);if(!result)add(&stats.rwlock_initializations);return result;}
    return EINVAL;
}
static int acquire(void *p,unsigned kind,void **out){
    if(!available() || !p)return EINVAL;
    uint32_t *word=p;unsigned spins=0;
    for(;;){uint32_t old=__atomic_load_n(word,__ATOMIC_ACQUIRE);
        if(old==tag(kind,READY)){*out=(unsigned char*)p+8;return 0;}
        if(old==tag(kind,INITIALIZING)){
            if(++spins>=64){api.sleep_ns(50000);spins=0;}
#if defined(__aarch64__)
            else __asm__ volatile("yield":::"memory");
#endif
            continue;
        }
        if(old>2 || (kind!=MUTEX && old))return EINVAL;
        uint32_t expected=old;if(!__atomic_compare_exchange_n(word,&expected,tag(kind,INITIALIZING),0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
        int result=init_storage(p,kind,(unsigned)old);__atomic_store_n(word,result?old:tag(kind,READY),__ATOMIC_RELEASE);
        if(result)return result;
    }
}
static int explicit_init(void *p,unsigned kind,unsigned value){
    if(!available() || !p)return EINVAL;__atomic_store_n((uint32_t*)p,tag(kind,INITIALIZING),__ATOMIC_RELAXED);
    int result=init_storage(p,kind,value);__atomic_store_n((uint32_t*)p,tag(kind,result?DEAD:READY),__ATOMIC_RELEASE);return result;
}
static int destroy(void *p,unsigned kind){
    void *data;int result=acquire(p,kind,&data);if(result)return result;
    if(busy(p))return EBUSY;
    result=kind==MUTEX?api.mutex_destroy(data):kind==COND?api.cond_destroy(data):api.rwlock_destroy(data);
    if(!result)__atomic_store_n((uint32_t*)p,tag(kind,DEAD),__ATOMIC_RELEASE);return result;
}
static int attr_get(const pthread_attr_t *p,Attr *a){if(!p)return EINVAL;memcpy(a,p,sizeof(*a));return a->magic==ATTR_MAGIC?0:EINVAL;}
int PUBLIC(pthread_attr_init)(pthread_attr_t *p){if(!available())return EAGAIN;Attr a={ATTR_MAGIC,api.default_stack,PTHREAD_CREATE_JOINABLE};memset(p,0,sizeof(*p));memcpy(p,&a,sizeof(a));return 0;}
int PUBLIC(pthread_attr_destroy)(pthread_attr_t *p){Attr a;int r=attr_get(p,&a);if(!r)memset(p,0,sizeof(*p));return r;}
int PUBLIC(pthread_attr_setstacksize)(pthread_attr_t *p,size_t n){Attr a;int r=attr_get(p,&a);if(r)return r;if(n<PTHREAD_STACK_MIN)return EINVAL;a.stack=n;memcpy(p,&a,sizeof(a));return 0;}
int PUBLIC(pthread_attr_getstacksize)(const pthread_attr_t *p,size_t *n){Attr a;int r=attr_get(p,&a);if(!r)*n=a.stack;return r;}
int PUBLIC(pthread_attr_setdetachstate)(pthread_attr_t *p,int value){Attr a;int r=attr_get(p,&a);if(r)return r;if(value!=PTHREAD_CREATE_JOINABLE && value!=PTHREAD_CREATE_DETACHED)return EINVAL;a.detach=value;memcpy(p,&a,sizeof(a));return 0;}
int PUBLIC(pthread_attr_getdetachstate)(const pthread_attr_t *p,int *value){Attr a;int r=attr_get(p,&a);if(!r)*value=a.detach;return r;}
int PUBLIC(pthread_create)(pthread_t *out,const pthread_attr_t *attributes,void *(*entry)(void *),void *argument){
    if(!available())return EAGAIN;Attr a={ATTR_MAGIC,api.default_stack,PTHREAD_CREATE_JOINABLE};int r=attributes?attr_get(attributes,&a):0;if(r)return r;
    if(a.detach==PTHREAD_CREATE_DETACHED && !api.detached_code_resident)return ENOTSUP;
    size_t mask=api.stack_page-1;if(a.stack>SIZE_MAX-mask)return EINVAL;size_t stack=(a.stack+mask)&~mask;
    _Alignas(16) unsigned char native[256]={0};r=api.thread_attr_init(native);if(r)return r;
    r=api.thread_attr_stack(native,stack);if(!r)r=api.thread_attr_detach(native,a.detach);
    uint64_t handle=0;if(!r)r=api.thread_create(&handle,native,entry,argument);
    int cleanup=api.thread_attr_destroy(native);
    /* A created thread is already live; a cleanup failure cannot revoke its
     * handle. Providers must make POD attribute destruction infallible. */
    if(!r){*out=(pthread_t)(uintptr_t)handle;add(&stats.created);if(a.detach==PTHREAD_CREATE_DETACHED)add(&stats.detached);return 0;}
    (void)cleanup;return r;
}
int PUBLIC(pthread_join)(pthread_t thread,void **out){if(!available())return EINVAL;int r=api.thread_join((uint64_t)(uintptr_t)thread,out);if(!r)add(&stats.joined);return r;}
int PUBLIC(pthread_detach)(pthread_t thread){if(!available())return EINVAL;if(!api.detached_code_resident)return ENOTSUP;int r=api.thread_detach((uint64_t)(uintptr_t)thread);if(!r)add(&stats.detached);return r;}
pthread_t PUBLIC(pthread_self)(void){return available()?(pthread_t)(uintptr_t)api.thread_self():(pthread_t)0;}
int PUBLIC(pthread_equal)(pthread_t a,pthread_t b){return a==b;}
int PUBLIC(pthread_mutexattr_init)(pthread_mutexattr_t *p){memset(p,0,sizeof(*p));return 0;}
int PUBLIC(pthread_mutexattr_destroy)(pthread_mutexattr_t *p){memset(p,0,sizeof(*p));return 0;}
int PUBLIC(pthread_mutexattr_settype)(pthread_mutexattr_t *p,int kind){if(kind<0 || kind>2)return EINVAL;memcpy(p,&kind,sizeof(kind));return 0;}
int PUBLIC(pthread_mutexattr_gettype)(const pthread_mutexattr_t *p,int *kind){memcpy(kind,p,sizeof(*kind));return *kind>=0 && *kind<=2?0:EINVAL;}
int PUBLIC(pthread_mutex_init)(pthread_mutex_t *p,const pthread_mutexattr_t *a){int kind=0;if(a)memcpy(&kind,a,sizeof(kind));if(kind<0 || kind>2)return EINVAL;return explicit_init(p,MUTEX,(unsigned)kind);}
int PUBLIC(pthread_mutex_destroy)(pthread_mutex_t *p){return destroy(p,MUTEX);}
#define MUTEX_OP(name,member) int PUBLIC(name)(pthread_mutex_t *p){void *data;int r=acquire(p,MUTEX,&data);if(r)return r;use(p);r=api.member(data);if(r)unuse(p);return r;}
MUTEX_OP(pthread_mutex_lock,mutex_lock)
MUTEX_OP(pthread_mutex_trylock,mutex_trylock)
/* A release can let another thread destroy and reuse this storage before the
 * provider returns. Drop our ownership reference BEFORE publication. A failed
 * provider unlock must retain ownership, so restoring the reference is safe. */
int PUBLIC(pthread_mutex_unlock)(pthread_mutex_t *p){void *data;int r=acquire(p,MUTEX,&data);if(r)return r;unuse(p);r=api.mutex_unlock(data);if(r)use(p);return r;}
int PUBLIC(pthread_condattr_init)(pthread_condattr_t *p){memset(p,0,sizeof(*p));return 0;}
int PUBLIC(pthread_condattr_destroy)(pthread_condattr_t *p){memset(p,0,sizeof(*p));return 0;}
int PUBLIC(pthread_condattr_setclock)(pthread_condattr_t *p,clockid_t value){if(value!=CLOCK_REALTIME && value!=CLOCK_MONOTONIC)return EINVAL;int n=value;memcpy(p,&n,sizeof(n));return 0;}
int PUBLIC(pthread_condattr_getclock)(const pthread_condattr_t *p,clockid_t *out){int value;memcpy(&value,p,sizeof(value));*out=value;return value==CLOCK_REALTIME || value==CLOCK_MONOTONIC?0:EINVAL;}
int PUBLIC(pthread_cond_init)(pthread_cond_t *p,const pthread_condattr_t *a){int clock=CLOCK_REALTIME;if(a)memcpy(&clock,a,sizeof(clock));if(clock!=CLOCK_REALTIME && clock!=CLOCK_MONOTONIC)return EINVAL;return explicit_init(p,COND,(unsigned)clock);}
int PUBLIC(pthread_cond_destroy)(pthread_cond_t *p){return destroy(p,COND);}
#define COND_OP(name,member) int PUBLIC(name)(pthread_cond_t *p){void *data;int r=acquire(p,COND,&data);return r?r:api.member(data);}
COND_OP(pthread_cond_signal,cond_signal)
COND_OP(pthread_cond_broadcast,cond_broadcast)
/* The condition may be destroyed after a waiter is unblocked, before it has
 * reacquired its companion mutex. Never borrow its storage after the callback.
 * The mutex ownership reference remains held through the entire wait. */
int PUBLIC(pthread_cond_wait)(pthread_cond_t *p,pthread_mutex_t *m){void *c,*mutex;int r=acquire(p,COND,&c);if(!r)r=acquire(m,MUTEX,&mutex);if(r)return r;add(&stats.waits);return api.cond_wait(c,mutex);}
static int timespec_copy(const struct timespec *time,Ps2SdkTimespec *out){if(!time || time->tv_nsec<0 || time->tv_nsec>=1000000000)return EINVAL;out->seconds=time->tv_sec;out->nanoseconds=time->tv_nsec;return 0;}
int PUBLIC(pthread_cond_timedwait)(pthread_cond_t *p,pthread_mutex_t *m,const struct timespec *time){void *c,*mutex;Ps2SdkTimespec t;int r=timespec_copy(time,&t);if(!r)r=acquire(p,COND,&c);if(!r)r=acquire(m,MUTEX,&mutex);if(r)return r;add(&stats.waits);return api.cond_timedwait(c,mutex,&t);}
int PUBLIC(pthread_cond_clockwait)(pthread_cond_t *p,pthread_mutex_t *m,clockid_t clock,const struct timespec *time){void *c,*mutex;Ps2SdkTimespec t;if(!api.cond_clockwait)return ENOTSUP;int r=timespec_copy(time,&t);if(!r)r=acquire(p,COND,&c);if(!r)r=acquire(m,MUTEX,&mutex);if(r)return r;add(&stats.waits);return api.cond_clockwait(c,mutex,clock,&t);}
int PUBLIC(pthread_rwlock_init)(pthread_rwlock_t *p,const pthread_rwlockattr_t *a){return a?ENOTSUP:explicit_init(p,RWLOCK,0);}
int PUBLIC(pthread_rwlock_destroy)(pthread_rwlock_t *p){return destroy(p,RWLOCK);}
#define RW_OP(name,member) int PUBLIC(name)(pthread_rwlock_t *p){void *data;int r=acquire(p,RWLOCK,&data);if(r)return r;use(p);r=api.member(data);if(r)unuse(p);return r;}
RW_OP(pthread_rwlock_rdlock,rwlock_read)
RW_OP(pthread_rwlock_wrlock,rwlock_write)
int PUBLIC(pthread_rwlock_unlock)(pthread_rwlock_t *p){void *data;int r=acquire(p,RWLOCK,&data);if(r)return r;unuse(p);r=api.rwlock_unlock(data);if(r)use(p);return r;}
static int sem_result(int result){if(result<0)errno=*api.sdk_errno_location();return result;}
int PUBLIC(sem_init)(sem_t *p,int shared,unsigned value){
    if(!available() || !api.sem_bytes){errno=ENOTSUP;return -1;}if(shared){errno=ENOTSUP;return -1;}
    __atomic_store_n((uint32_t*)p,tag(SEM,INITIALIZING),__ATOMIC_RELAXED);__atomic_store_n((uint32_t*)p+1,0,__ATOMIC_RELAXED);memset((unsigned char*)p+8,0,api.sem_bytes);
    int r=sem_result(api.sem_init((unsigned char*)p+8,0,value));__atomic_store_n((uint32_t*)p,tag(SEM,r?DEAD:READY),__ATOMIC_RELEASE);if(!r)add(&stats.sem_initializations);return r;
}
static void *sem_data(sem_t *p){if(!available() || !api.sem_bytes || __atomic_load_n((uint32_t*)p,__ATOMIC_ACQUIRE)!=tag(SEM,READY)){errno=EINVAL;return 0;}return (unsigned char*)p+8;}
int PUBLIC(sem_destroy)(sem_t *p){void *data=sem_data(p);if(!data)return -1;if(busy(p)){errno=EBUSY;return -1;}int r=sem_result(api.sem_destroy(data));if(!r)__atomic_store_n((uint32_t*)p,tag(SEM,DEAD),__ATOMIC_RELEASE);return r;}
#define SEM_OP(name,member) int PUBLIC(name)(sem_t *p){void *data=sem_data(p);if(!data)return -1;use(p);int r=sem_result(api.member(data));unuse(p);return r;}
SEM_OP(sem_wait,sem_wait)
SEM_OP(sem_trywait,sem_trywait)
/* sem_post must not touch the object after publishing its token: the last
 * waiter can return, destroy a stack semaphore and reuse that stack immediately. */
int PUBLIC(sem_post)(sem_t *p){void *data=sem_data(p);return data?sem_result(api.sem_post(data)):-1;}
int PUBLIC(sem_timedwait)(sem_t *p,const struct timespec *time){void *data=sem_data(p);if(!data)return -1;Ps2SdkTimespec t;int r=timespec_copy(time,&t);if(r){errno=r;return -1;}use(p);r=sem_result(api.sem_timedwait(data,&t));unuse(p);return r;}
void ps2_sdk_threads_statistics(Ps2SdkThreadStatistics *out){
#define COPY(field) out->field=__atomic_load_n(&stats.field,__ATOMIC_RELAXED)
    COPY(created);COPY(joined);COPY(detached);COPY(mutex_initializations);COPY(cond_initializations);COPY(rwlock_initializations);COPY(sem_initializations);COPY(waits);
#undef COPY
}
