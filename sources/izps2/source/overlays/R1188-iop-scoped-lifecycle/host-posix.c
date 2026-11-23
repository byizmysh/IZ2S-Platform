#define _POSIX_C_SOURCE 200809L
#include <sb4/host-posix.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include "host-clock.h"
#ifdef __SWITCH2__
#include <reent.h>
#include <sys/lock.h>
#include <sys/time.h>
#endif

typedef struct {
    uint32_t (*allocate_aligned)(void **, size_t, size_t);
    void (*release)(void *);
    void (*sleep_ns)(uint64_t);
    int (*create_hosted_thread)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    uint32_t (*thread_wait_handle)(void);
    int (*thread_signaled)(uint32_t);
#define SB4_POSIX(name, result, params, args, unavailable, provider) result (*name) params;
#include "host-posix.def"
#undef SB4_POSIX
} HostedPosix;

static HostedPosix posix;
static atomic_uint bound;

typedef struct HostedThread {
    struct HostedThread *next;
    pthread_t thread;
    void *(*start)(void *);
    void *argument;
    atomic_bool published;
    uint32_t wait_handle;
    bool detached;
    bool joining;
} HostedThread;

static HostedThread *threads;
static atomic_flag threads_lock = ATOMIC_FLAG_INIT;
static unsigned thread_operations;
static bool closing;
static atomic_bool isolated_iop_live;

static bool ready(void) {
    return atomic_load_explicit(&bound, memory_order_acquire) == 2;
}

static void lock_threads(void) {
    while (atomic_flag_test_and_set_explicit(&threads_lock, memory_order_acquire)) {}
}

static void unlock_threads(void) {
    atomic_flag_clear_explicit(&threads_lock, memory_order_release);
}

uint32_t sb4HostPosixBind(const Sb4Compat *compat) {
    if (!sb4CompatValid(compat)) return EINVAL;
    HostedPosix candidate = {
        .allocate_aligned = compat->allocate_aligned,
        .release = compat->release,
        .sleep_ns = compat->sleep_ns,
    };
    candidate.create_hosted_thread = (int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *))
        compat->resolve_c("sb4SdCreateHostedThread");
    candidate.thread_wait_handle = (uint32_t (*)(void))compat->resolve_c("sb4HostThreadWaitHandle");
    candidate.thread_signaled = (int (*)(uint32_t))compat->resolve_c("sb4HostThreadSignaled");
#define SB4_POSIX(name, result, params, args, unavailable, provider) \
    candidate.name = (result (*) params)compat->resolve_c(#name); \
    if (!candidate.name) return ENOSYS;
#define SB4_POSIX_OPTIONAL(name, result, params, args, unavailable, provider) \
    candidate.name = (result (*) params)compat->resolve_c(#name);
#include "host-posix.def"
#undef SB4_POSIX_OPTIONAL
#undef SB4_POSIX
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong(&bound, &expected, 1)) return EBUSY;
    posix = candidate;
    atomic_store_explicit(&bound, 2, memory_order_release);
    return 0;
}

int sb4HostPosixCanReadStorageFromThreads(void) {
    return ready() && posix.create_hosted_thread != NULL;
}

static bool managed_detach(void) {
    return posix.thread_wait_handle && posix.thread_signaled && posix.pthread_attr_getdetachstate;
}

extern bool iz_ps2_iop_take_scoped_create(void);
extern bool iz_ps2_iop_try_native_join(pthread_t thread, void **result);

static int join_tracked_thread(HostedThread *entry, void **result) {
    const int status = posix.pthread_join(entry->thread, result);
    lock_threads();
    if (status) entry->joining = false;
    else {
        HostedThread **cursor = &threads;
        while (*cursor != entry) cursor = &(*cursor)->next;
        *cursor = entry->next;
        /* Keep admission accounting live until the record is released. */
        ++thread_operations;
    }
    unlock_threads();
    if (!status) {
        posix.release(entry);
        lock_threads();
        --thread_operations;
        unlock_threads();
    }
    return status;
}

static void reap_detached_threads(void) {
    if (!managed_detach()) return;
    for (;;) {
        lock_threads();
        HostedThread *entry = threads;
        while (entry && (!entry->detached || entry->joining ||
                         !entry->wait_handle || !posix.thread_signaled(entry->wait_handle)))
            entry = entry->next;
        if (entry) entry->joining = true;
        unlock_threads();
        if (!entry || join_tracked_thread(entry, NULL)) break;
    }
}

uint32_t sb4HostPosixQuiesce(void) {
    if (!ready()) return ENOSYS;
    if (atomic_load_explicit(&isolated_iop_live, memory_order_acquire)) return EBUSY;
    lock_threads();
    closing = true;
    unlock_threads();
    reap_detached_threads();
    lock_threads();
    const uint32_t result = thread_operations || threads ? EBUSY : 0;
    unlock_threads();
    return result;
}

static void *thread_start(void *context) {
    HostedThread *entry = context;
    while (!atomic_load_explicit(&entry->published, memory_order_acquire))
        posix.sleep_ns(100000);
    if (managed_detach()) {
        lock_threads();
        entry->wait_handle = posix.thread_wait_handle();
        unlock_threads();
    }
    return entry->start(entry->argument);
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
    if (!ready()) return ENOSYS;
    if (!thread || !start) return EINVAL;
    // The VM owner starts and reaps this joinable worker before quiescence.
    // It needs the SDK's SD read scope, with a separate lifetime receipt.
    if (iz_ps2_iop_take_scoped_create()) {
        if (!posix.create_hosted_thread) return ENOSYS;
        if (atomic_exchange_explicit(&isolated_iop_live, true, memory_order_acq_rel)) return EBUSY;
        *thread = (pthread_t)0;
        const int status = posix.create_hosted_thread(thread, attr, start, arg);
        if (status && !*thread)
            atomic_store_explicit(&isolated_iop_live, false, memory_order_release);
        return status;
    }
    lock_threads();
    if (closing) {
        unlock_threads();
        return EBUSY;
    }
    ++thread_operations;
    unlock_threads();
    reap_detached_threads();

    int detach_state = PTHREAD_CREATE_JOINABLE;
    int result = 0;
    pthread_attr_t joinable;
    if (attr && managed_detach()) {
        result = posix.pthread_attr_getdetachstate(attr, &detach_state);
        if (!result && detach_state == PTHREAD_CREATE_DETACHED) {
            joinable = *attr;
            result = posix.pthread_attr_setdetachstate(&joinable, PTHREAD_CREATE_JOINABLE);
            attr = &joinable;
        }
    }
    void *data = NULL;
    if (!result) result = (int)posix.allocate_aligned(&data, _Alignof(HostedThread), sizeof(HostedThread));
    HostedThread *entry = data;
    if (!result && !entry) result = ENOMEM;
    if (!result) {
        entry->start = start;
        entry->argument = arg;
        entry->wait_handle = 0;
        entry->detached = detach_state == PTHREAD_CREATE_DETACHED;
        entry->joining = false;
        atomic_init(&entry->published, false);
        result = posix.create_hosted_thread ? posix.create_hosted_thread(thread, attr, thread_start, entry)
                                           : posix.pthread_create(thread, attr, thread_start, entry);
    }
    if (result && entry) posix.release(entry);

    lock_threads();
    if (!result) {
        entry->thread = *thread;
        entry->next = threads;
        threads = entry;
        atomic_store_explicit(&entry->published, true, memory_order_release);
    }
    --thread_operations;
    unlock_threads();
    return result;
}

static HostedThread *find_thread(pthread_t thread) {
    for (HostedThread *entry = threads; entry; entry = entry->next)
        if (posix.pthread_equal(entry->thread, thread)) return entry;
    return NULL;
}

int __wrap_pthread_detach(pthread_t thread) {
    if (!ready()) return ENOSYS;
    if (!managed_detach()) return posix.pthread_detach(thread);
    lock_threads();
    HostedThread *entry = find_thread(thread);
    if (!entry) {
        unlock_threads();
        return posix.pthread_detach(thread);
    }
    if (entry->joining || entry->detached) {
        unlock_threads();
        return EINVAL;
    }
    /* Keep the provider thread joinable until its exit has been observed. */
    entry->detached = true;
    unlock_threads();
    reap_detached_threads();
    return 0;
}

int __wrap_pthread_join(pthread_t thread, void **result) {
    if (!ready()) return ENOSYS;
    if (atomic_load_explicit(&isolated_iop_live, memory_order_acquire) &&
        iz_ps2_iop_try_native_join(thread, result)) {
        atomic_store_explicit(&isolated_iop_live, false, memory_order_release);
        return 0;
    }
    lock_threads();
    HostedThread *entry = find_thread(thread);
    if (entry && (entry->joining || entry->detached)) {
        unlock_threads();
        return EINVAL;
    }
    if (entry) entry->joining = true;
    unlock_threads();

    return entry ? join_tracked_thread(entry, result) : posix.pthread_join(thread, result);
}

static int unavailable_errno(void) {
    errno = ENOSYS;
    return -1;
}

static int hosted_clock_sleep(clockid_t clock, int flags, const struct timespec *time,
                              struct timespec *remaining) {
    if (!ready()) return ENOSYS;
    return sb4_clock_sleep(clock, flags, time, remaining, posix.clock_gettime, posix.nanosleep);
}

#ifdef __SWITCH2__
static int unavailable_reentrant(struct _reent *context) {
    context->_errno = ENOSYS;
    return -1;
}
#endif

#define SB4_POSIX(name, result, params, args, unavailable, provider) \
    result __wrap_##name params { \
        return ready() ? posix.name args : (unavailable); \
    }
#define SB4_POSIX_VOID(name, params, args) \
    void __wrap_##name params { \
        if (ready()) posix.name args; \
    }
#define SB4_POSIX_MANAGED(name, result, params, args, unavailable, provider)
#define SB4_POSIX_OPTIONAL(name, result, params, args, unavailable, provider) \
    result __wrap_##name params { \
        return ready() && posix.name ? posix.name args : (unavailable); \
    }
#include "host-posix.def"
#undef SB4_POSIX_OPTIONAL
#undef SB4_POSIX_MANAGED
#undef SB4_POSIX_VOID
#undef SB4_POSIX
