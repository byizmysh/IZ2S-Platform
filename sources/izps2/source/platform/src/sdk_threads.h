#ifndef PS2_SDK_THREADS_H
#define PS2_SDK_THREADS_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {int64_t seconds,nanoseconds;} Ps2SdkTimespec;
/* SDK objects occupy separate inline storage inside the port's opaque POSIX
 * objects. The provider supplies verified storage bounds, never Linux types.
 * Inline sync objects must own no external resources: libstdc++ can destroy
 * an ordinary std::mutex without calling pthread_mutex_destroy.
 * Release callbacks must not access object storage after publishing an unlock,
 * token, or final notification. A successful waiter can immediately destroy it.
 * Failed mutex/rwlock unlocks must retain ownership. Condition callbacks must
 * stop accessing the condition before reacquiring their companion mutex.
 * Destroying a condition with still-blocked waiters is invalid. */
typedef struct {
    size_t thread_attr_bytes,mutex_attr_bytes,cond_attr_bytes,mutex_bytes,cond_bytes,rwlock_bytes,sem_bytes;
    size_t stack_page,default_stack;
    uint32_t inline_sync_no_resources;
    uint32_t detached_code_resident; /* code remains mapped for detached callbacks */
    void (*sleep_ns)(uint64_t);
    int (*thread_attr_init)(void *);
    int (*thread_attr_destroy)(void *);
    int (*thread_attr_stack)(void *,size_t);
    int (*thread_attr_detach)(void *,int);
    int (*thread_create)(uint64_t *,const void *,void *(*)(void *),void *);
    int (*thread_join)(uint64_t,void **);
    int (*thread_detach)(uint64_t);
    uint64_t (*thread_self)(void);
    int (*mutex_attr_init)(void *);
    int (*mutex_attr_destroy)(void *);
    int (*mutex_attr_type)(void *,int);
    int (*mutex_init)(void *,const void *);
    int (*mutex_destroy)(void *);
    int (*mutex_lock)(void *);
    int (*mutex_trylock)(void *);
    int (*mutex_unlock)(void *);
    int (*cond_attr_init)(void *);
    int (*cond_attr_destroy)(void *);
    int (*cond_attr_clock)(void *,int);
    int (*cond_init)(void *,const void *);
    int (*cond_destroy)(void *);
    int (*cond_wait)(void *,void *);
    int (*cond_timedwait)(void *,void *,const Ps2SdkTimespec *);
    int (*cond_clockwait)(void *,void *,int,const Ps2SdkTimespec *);
    int (*cond_signal)(void *);
    int (*cond_broadcast)(void *);
    int (*rwlock_init)(void *,const void *);
    int (*rwlock_destroy)(void *);
    int (*rwlock_read)(void *);
    int (*rwlock_write)(void *);
    int (*rwlock_unlock)(void *);
    int (*sem_init)(void *,int,unsigned);
    int (*sem_destroy)(void *);
    int (*sem_wait)(void *);
    int (*sem_trywait)(void *);
    int (*sem_timedwait)(void *,const Ps2SdkTimespec *);
    int (*sem_post)(void *);
    int *(*sdk_errno_location)(void);
} Ps2SdkThreadApi;
typedef struct {
    uint64_t created,joined,detached,mutex_initializations,cond_initializations,rwlock_initializations,sem_initializations,waits;
} Ps2SdkThreadStatistics;
/* Before any C++ constructors; workers must be quiescent during binding.
 * Profile sizes are capacities established by the provider. No SDK profile
 * or hardware qualification is inferred from a passing Linux provider. */
int ps2_sdk_threads_initialize(const Ps2SdkThreadApi *);
void ps2_sdk_threads_statistics(Ps2SdkThreadStatistics *);
#ifdef __cplusplus
}
#endif
#endif
