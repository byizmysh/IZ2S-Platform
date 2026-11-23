/* This image owns every POSIX pthread/futex object. Native HBXL intercepts
 * libc clone/futex syscalls into its real SDK thread/arbitration implementation;
 * no SDK pthread object crosses the private public-SDK-adapter layout.
 * The SAME source executes with ordinary Linux syscalls only under QEMU. */
#define _GNU_SOURCE
#include "sdk_threads.h"
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"thread provider line %d: %s\n",__LINE__,#x);abort();}}while(0)
#define MU_MAGIC 0x534d5558u
#define CV_MAGIC 0x53434f4eu
#define RW_MAGIC 0x5352574cu
#define SEM_MAGIC 0x5353454du
typedef struct {uint32_t state,owner,depth,kind,magic;} Mutex;
typedef struct {uint32_t sequence;int clock;uint32_t magic;} Condition;
typedef struct {Mutex mutex;Condition condition;uint32_t readers,writer,waiting_writers,magic;} Rwlock;
typedef struct {uint32_t count,magic;} Semaphore;
extern int __real_pthread_attr_init(pthread_attr_t *);
extern int __real_pthread_attr_destroy(pthread_attr_t *);
extern int __real_pthread_attr_setstacksize(pthread_attr_t *,size_t);
extern int __real_pthread_attr_setdetachstate(pthread_attr_t *,int);
extern int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
extern int __real_pthread_join(pthread_t,void **);
extern int __real_pthread_detach(pthread_t);
extern pthread_t __real_pthread_self(void);
static void sleep_ns(uint64_t ns){struct timespec t={(time_t)(ns/1000000000),(long)(ns%1000000000)};nanosleep(&t,0);}
static int attr_init(void *p){return __real_pthread_attr_init(p);}
static int attr_destroy(void *p){return __real_pthread_attr_destroy(p);}
static int attr_stack(void *p,size_t n){CHECK(n && !(n&4095));return __real_pthread_attr_setstacksize(p,n);}
static int attr_detach(void *p,int n){return __real_pthread_attr_setdetachstate(p,n);}
#ifdef PS2_HBXL_SDK_STACKS
#include "hbxl_stack_pool.h"
#endif
static int create(uint64_t *out,const void *a,void *(*entry)(void *),void *argument){
#ifdef PS2_HBXL_SDK_STACKS
    const int scope_error=hbxl_stack_scope_enter();
    if(scope_error){fprintf(stderr,"PS2 thread create: SDK stack scope error %d\n",scope_error);return EAGAIN;}
#endif
    pthread_t t;int r=__real_pthread_create(&t,a,entry,argument);
#ifdef PS2_HBXL_SDK_STACKS
    CHECK(!hbxl_stack_scope_leave());
#endif
    if(r)fprintf(stderr,"PS2 thread create: libc error %d (errno %d)\n",r,errno);
    if(!r)*out=(uintptr_t)t;return r;
}
static int join(uint64_t thread,void **out){return __real_pthread_join((pthread_t)(uintptr_t)thread,out);}
static int detach(uint64_t thread){return __real_pthread_detach((pthread_t)(uintptr_t)thread);}
static uint64_t self(void){return (uintptr_t)__real_pthread_self();}
static int scalar_init(void *p){*(int*)p=0;return 0;}
static int scalar_destroy(void *p){*(int*)p=0;return 0;}
static int scalar_type(void *p,int n){if(n<0 || n>2)return EINVAL;*(int*)p=n;return 0;}
static int scalar_clock(void *p,int n){if(n!=CLOCK_REALTIME && n!=CLOCK_MONOTONIC)return EINVAL;*(int*)p=n;return 0;}
static int futex_wait(uint32_t *word,uint32_t value,int clock,const Ps2SdkTimespec *time){
    struct timespec native;int flags=FUTEX_WAIT_PRIVATE;
    if(time){native=(struct timespec){time->seconds,time->nanoseconds};flags=FUTEX_WAIT_BITSET_PRIVATE|(clock==CLOCK_REALTIME?FUTEX_CLOCK_REALTIME:0);}
    long r=syscall(SYS_futex,word,flags,value,time?&native:0,0,FUTEX_BITSET_MATCH_ANY);
    if(!r || errno==EAGAIN || errno==EINTR)return 0;return errno;
}
static void futex_wake(uint32_t *word,int count){CHECK(syscall(SYS_futex,word,FUTEX_WAKE_PRIVATE,count,0,0,0)>=0);}
static int mutex_init(void *p,const void *a){Mutex *m=p;memset(m,0,sizeof(*m));m->kind=a?*(const int*)a:0;m->magic=MU_MAGIC;return 0;}
static int mutex_destroy(void *p){Mutex *m=p;CHECK(m->magic==MU_MAGIC);if(__atomic_load_n(&m->state,__ATOMIC_ACQUIRE))return EBUSY;m->magic=0;return 0;}
static int mutex_lock(void *p){
    Mutex *m=p;CHECK(m->magic==MU_MAGIC);uint32_t tid=(uint32_t)syscall(SYS_gettid);
    if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)==tid){if(m->kind!=1)return EDEADLK;if(m->depth==65535)return EAGAIN;m->depth++;return 0;}
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&m->state,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
        while(__atomic_exchange_n(&m->state,2,__ATOMIC_ACQUIRE))futex_wait(&m->state,2,CLOCK_MONOTONIC,0);
    }
    __atomic_store_n(&m->owner,tid,__ATOMIC_RELAXED);m->depth=1;return 0;
}
static int mutex_trylock(void *p){Mutex *m=p;CHECK(m->magic==MU_MAGIC);uint32_t tid=(uint32_t)syscall(SYS_gettid);
    if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)==tid && m->kind==1){if(m->depth==65535)return EAGAIN;m->depth++;return 0;}
    uint32_t expected=0;if(!__atomic_compare_exchange_n(&m->state,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return EBUSY;
    __atomic_store_n(&m->owner,tid,__ATOMIC_RELAXED);m->depth=1;return 0;
}
static int mutex_unlock(void *p){Mutex *m=p;CHECK(m->magic==MU_MAGIC);if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=(uint32_t)syscall(SYS_gettid) || !m->depth)return EPERM;
    if(--m->depth)return 0;__atomic_store_n(&m->owner,0,__ATOMIC_RELAXED);if(__atomic_exchange_n(&m->state,0,__ATOMIC_RELEASE)==2)futex_wake(&m->state,1);return 0;
}
static int cond_init(void *p,const void *a){Condition *c=p;*c=(Condition){0,a?*(const int*)a:CLOCK_REALTIME,CV_MAGIC};return 0;}
static int cond_destroy(void *p){Condition *c=p;CHECK(c->magic==CV_MAGIC);c->magic=0;return 0;}
static int cond_clockwait(void *p,void *mutex,int clock,const Ps2SdkTimespec *time){
    Condition *c=p;Mutex *m=mutex;CHECK(c->magic==CV_MAGIC && m->magic==MU_MAGIC);
    if(clock!=CLOCK_REALTIME && clock!=CLOCK_MONOTONIC)return EINVAL;
    if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=(uint32_t)syscall(SYS_gettid) || m->depth!=1)return EPERM;
    uint32_t sequence=__atomic_load_n(&c->sequence,__ATOMIC_ACQUIRE);int r=mutex_unlock(m);if(r)return r;
    r=futex_wait(&c->sequence,sequence,clock,time);int lock=mutex_lock(m);return lock?lock:r;
}
static int cond_wait(void *p,void *m){Condition *c=p;return cond_clockwait(p,m,c->clock,0);}
static int cond_timedwait(void *p,void *m,const Ps2SdkTimespec *t){Condition *c=p;return cond_clockwait(p,m,c->clock,t);}
static int cond_signal(void *p){Condition *c=p;CHECK(c->magic==CV_MAGIC);__atomic_fetch_add(&c->sequence,1,__ATOMIC_RELEASE);futex_wake(&c->sequence,1);return 0;}
static int cond_broadcast(void *p){Condition *c=p;CHECK(c->magic==CV_MAGIC);__atomic_fetch_add(&c->sequence,1,__ATOMIC_RELEASE);futex_wake(&c->sequence,INT_MAX);return 0;}
static int rw_init(void *p,const void *a){CHECK(!a);Rwlock *r=p;memset(r,0,sizeof(*r));mutex_init(&r->mutex,0);cond_init(&r->condition,0);r->magic=RW_MAGIC;return 0;}
static int rw_destroy(void *p){Rwlock *r=p;CHECK(r->magic==RW_MAGIC);if(r->readers || r->writer || r->waiting_writers)return EBUSY;r->magic=0;return 0;}
static int rw_read(void *p){Rwlock *r=p;CHECK(r->magic==RW_MAGIC);mutex_lock(&r->mutex);while(r->writer || r->waiting_writers)cond_wait(&r->condition,&r->mutex);r->readers++;return mutex_unlock(&r->mutex);}
static int rw_write(void *p){Rwlock *r=p;CHECK(r->magic==RW_MAGIC);mutex_lock(&r->mutex);r->waiting_writers++;while(r->writer || r->readers)cond_wait(&r->condition,&r->mutex);r->waiting_writers--;r->writer=(uint32_t)syscall(SYS_gettid);return mutex_unlock(&r->mutex);}
static int rw_unlock(void *p){Rwlock *r=p;CHECK(r->magic==RW_MAGIC);mutex_lock(&r->mutex);if(r->writer){CHECK(r->writer==(uint32_t)syscall(SYS_gettid));r->writer=0;}else{CHECK(r->readers);r->readers--;}cond_broadcast(&r->condition);int result=mutex_unlock(&r->mutex);return result;}
static int semaphore_init(void *p,int shared,unsigned count){CHECK(!shared);if(count>INT_MAX){errno=EINVAL;return -1;}*(Semaphore*)p=(Semaphore){count,SEM_MAGIC};return 0;}
static int semaphore_destroy(void *p){Semaphore *s=p;CHECK(s->magic==SEM_MAGIC);s->magic=0;return 0;}
static int semaphore_try(void *p){Semaphore *s=p;CHECK(s->magic==SEM_MAGIC);uint32_t value=__atomic_load_n(&s->count,__ATOMIC_ACQUIRE);
    while(value)if(__atomic_compare_exchange_n(&s->count,&value,value-1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;errno=EAGAIN;return -1;}
static int semaphore_timed(void *p,const Ps2SdkTimespec *time){Semaphore *s=p;CHECK(s->magic==SEM_MAGIC);while(semaphore_try(p)){
    int r=futex_wait(&s->count,0,CLOCK_REALTIME,time);if(r){errno=r;return -1;}}
    return 0;
}
static int semaphore_wait(void *p){return semaphore_timed(p,0);}
static int semaphore_post(void *p){Semaphore *s=p;CHECK(s->magic==SEM_MAGIC);uint32_t value=__atomic_load_n(&s->count,__ATOMIC_RELAXED);
    do{if(value==INT_MAX){errno=EOVERFLOW;return -1;}}while(!__atomic_compare_exchange_n(&s->count,&value,value+1,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED));futex_wake(&s->count,1);return 0;}
static int *sdk_errno_location(void){return &errno;}
__attribute__((constructor(70))) static void initialize_provider(void){
    Ps2SdkThreadApi a={
        .thread_attr_bytes=sizeof(pthread_attr_t),.mutex_attr_bytes=4,.cond_attr_bytes=4,.mutex_bytes=sizeof(Mutex),.cond_bytes=sizeof(Condition),.rwlock_bytes=sizeof(Rwlock),.sem_bytes=sizeof(Semaphore),
        .stack_page=4096,.default_stack=2*1024*1024,.inline_sync_no_resources=1,.detached_code_resident=1,.sleep_ns=sleep_ns,
        .thread_attr_init=attr_init,.thread_attr_destroy=attr_destroy,.thread_attr_stack=attr_stack,.thread_attr_detach=attr_detach,
        .thread_create=create,.thread_join=join,.thread_detach=detach,.thread_self=self,
        .mutex_attr_init=scalar_init,.mutex_attr_destroy=scalar_destroy,.mutex_attr_type=scalar_type,.mutex_init=mutex_init,.mutex_destroy=mutex_destroy,.mutex_lock=mutex_lock,.mutex_trylock=mutex_trylock,.mutex_unlock=mutex_unlock,
        .cond_attr_init=scalar_init,.cond_attr_destroy=scalar_destroy,.cond_attr_clock=scalar_clock,.cond_init=cond_init,.cond_destroy=cond_destroy,.cond_wait=cond_wait,.cond_timedwait=cond_timedwait,.cond_clockwait=cond_clockwait,.cond_signal=cond_signal,.cond_broadcast=cond_broadcast,
        .rwlock_init=rw_init,.rwlock_destroy=rw_destroy,.rwlock_read=rw_read,.rwlock_write=rw_write,.rwlock_unlock=rw_unlock,
        .sem_init=semaphore_init,.sem_destroy=semaphore_destroy,.sem_wait=semaphore_wait,.sem_trywait=semaphore_try,.sem_timedwait=semaphore_timed,.sem_post=semaphore_post,.sdk_errno_location=sdk_errno_location};
    CHECK(!ps2_sdk_threads_initialize(&a));
}
