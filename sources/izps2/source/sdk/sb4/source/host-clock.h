#ifndef SB4_HOST_CLOCK_H
#define SB4_HOST_CLOCK_H

#include <errno.h>
#include <stdint.h>
#include <time.h>

typedef int (*Sb4ReadClock)(clockid_t, struct timespec *);
typedef int (*Sb4RelativeSleep)(const struct timespec *, struct timespec *);

static inline int sb4_time_before(const struct timespec *first, const struct timespec *second) {
    return first->tv_sec < second->tv_sec ||
        (first->tv_sec == second->tv_sec && first->tv_nsec < second->tv_nsec);
}

static inline int sb4_sleep_until(const struct timespec *deadline, Sb4ReadClock read_clock,
                                  Sb4RelativeSleep sleep_relative) {
    for (;;) {
        struct timespec now;
        if (read_clock(CLOCK_MONOTONIC, &now)) return errno;
        if (!sb4_time_before(&now, deadline)) return 0;

        uintmax_t seconds = (uintmax_t)deadline->tv_sec - (uintmax_t)now.tv_sec;
        long nanoseconds = deadline->tv_nsec - now.tv_nsec;
        if (nanoseconds < 0) {
            --seconds;
            nanoseconds += 1000000000L;
        }
        // Keep conversion to the launcher's signed nanosecond API in range.
        const struct timespec delay = seconds >= 3600 ?
            (struct timespec){3600, 0} : (struct timespec){(time_t)seconds, nanoseconds};
        if (sleep_relative(&delay, NULL)) return errno;
    }
}

static inline int sb4_clock_sleep(clockid_t clock, int flags, const struct timespec *time,
                                  struct timespec *remaining, Sb4ReadClock read_clock,
                                  Sb4RelativeSleep sleep_relative) {
    if (!time || time->tv_sec < 0 || time->tv_nsec < 0 || time->tv_nsec >= 1000000000L ||
        (flags != 0 && flags != TIMER_ABSTIME))
        return EINVAL;
    if (clock != CLOCK_MONOTONIC && (clock != CLOCK_REALTIME || flags != 0))
        return ENOTSUP;

    const int saved_errno = errno;
    int rc;
    if (flags == TIMER_ABSTIME)
        rc = sb4_sleep_until(time, read_clock, sleep_relative);
    else
        rc = sleep_relative(time, remaining) ? errno : 0;
    errno = saved_errno;
    return rc;
}

#endif
