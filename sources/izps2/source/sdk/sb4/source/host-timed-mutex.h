#ifndef SB4_HOST_TIMED_MUTEX_H
#define SB4_HOST_TIMED_MUTEX_H

#include <pthread.h>
#include "host-clock.h"

typedef int (*Sb4TryMutex)(pthread_mutex_t *);

static inline int sb4_mutex_try_until(pthread_mutex_t *mutex, const struct timespec *deadline,
                                       Sb4TryMutex try_lock, Sb4ReadClock read_clock,
                                       Sb4RelativeSleep sleep_relative) {
    int rc = try_lock(mutex);
    if (rc != EBUSY) return rc;
    if (!deadline || deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000L)
        return EINVAL;

    struct timespec now;
    if (read_clock(CLOCK_REALTIME, &now)) return errno;
    while (sb4_time_before(&now, deadline)) {
        const uintmax_t seconds = (uintmax_t)deadline->tv_sec - (uintmax_t)now.tv_sec;
        long ns = 1000000L;
        if (seconds <= 1) {
            const long remaining = (long)seconds * 1000000000L + deadline->tv_nsec - now.tv_nsec;
            if (remaining < ns) ns = remaining;
        }
        const struct timespec delay = {0, ns};
        if (sleep_relative(&delay, NULL) && errno != EINTR) return errno;
        if (read_clock(CLOCK_REALTIME, &now)) return errno;
        if (!sb4_time_before(&now, deadline)) break;
        rc = try_lock(mutex);
        if (rc != EBUSY) return rc;
    }
    return ETIMEDOUT;
}

static inline int sb4_mutex_timed_lock(pthread_mutex_t *mutex, const struct timespec *deadline,
                                        Sb4TryMutex try_lock, Sb4ReadClock read_clock,
                                        Sb4RelativeSleep sleep_relative) {
    const int saved_errno = errno;
    const int rc = sb4_mutex_try_until(mutex, deadline, try_lock, read_clock, sleep_relative);
    errno = saved_errno;
    return rc;
}

#endif
