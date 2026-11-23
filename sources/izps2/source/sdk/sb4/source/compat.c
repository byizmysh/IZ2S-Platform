#define _GNU_SOURCE
#include <sb4.h>
#include <sb4/native-services.h>
#include <sb4/host-posix.h>
#include <sb4/memory.h>
#include <sb4/storage.h>
#include <switch.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#ifdef __SWITCH2__
#include "host-clock.h"
#include <pthread.h>
#include "host-timed-mutex.h"
#include <reent.h>
#include <sched.h>
#include <semaphore.h>
#include <switch2/memory/heap.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

static uint32_t native_services_version(void) { return SB4_NATIVE_SERVICES_VERSION; }
#include <sys/lock.h>
#include <sys/time.h>

static uint32_t host_posix_version(void) { return SB4_HOST_POSIX_VERSION; }

static uint32_t host_thread_wait_handle(void) {
    const Thread *thread = threadGetSelf();
    return thread ? thread->handle : 0;
}

static int host_thread_signaled(uint32_t handle) {
    int32_t index;
    return handle && R_SUCCEEDED(svcWaitSynchronization(&index, &handle, 1, 0));
}

static int host_nanosleep(const struct timespec *delay, struct timespec *remaining) {
    (void)remaining;
    if (!delay || delay->tv_sec < 0 || delay->tv_nsec < 0 || delay->tv_nsec >= 1000000000 ||
        (uint64_t)delay->tv_sec > (uint64_t)(INT64_MAX - delay->tv_nsec) / 1000000000) {
        errno = EINVAL;
        return -1;
    }
    svcSleepThread((int64_t)delay->tv_sec * 1000000000 + delay->tv_nsec);
    return 0;
}

static int host_clock_nanosleep(clockid_t clock, int flags, const struct timespec *time,
                               struct timespec *remaining) {
    return sb4_clock_sleep(clock, flags, time, remaining, clock_gettime, host_nanosleep);
}

static int host_pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) {
    if (!mutex) return EINVAL;
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && mutexIsLockedByCurrentThread(&mutex->normal))
        return EDEADLK;
    return sb4_mutex_timed_lock(mutex, deadline, pthread_mutex_trylock, clock_gettime, host_nanosleep);
}
#endif

static int *errno_location(void) {
    return &errno;
}

#ifndef __SWITCH2__
static size_t strl_copy(char *destination, const char *source, size_t capacity) {
    const size_t length = strlen(source);
    if (capacity) {
        const size_t copied = length < capacity ? length : capacity - 1;
        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

static size_t strl_append(char *destination, const char *source, size_t capacity) {
    const size_t used = strnlen(destination, capacity);
    return used == capacity ? used + strlen(source) :
        used + strl_copy(destination + used, source, capacity - used);
}
#endif

static Sb4CFunction resolve_c(const char *name) {
    static const struct { const char *name; Sb4CFunction fn; } functions[] = {
#define IMPORT(fn) {#fn, (Sb4CFunction)(fn)}
        {"__errno_location", (Sb4CFunction)errno_location},
        IMPORT(abs), IMPORT(aligned_alloc), IMPORT(atof), IMPORT(atoi), IMPORT(bsearch),
        IMPORT(calloc), IMPORT(free), IMPORT(isalnum),
#ifdef __SWITCH2__
        {SB4_NATIVE_SERVICES_IMPORT, (Sb4CFunction)native_services_version},
#define SB4_NATIVE_SERVICE(fn) {#fn, (Sb4CFunction)(fn)},
#include "native-services.def"
#undef SB4_NATIVE_SERVICE
        IMPORT(heapGetFreeSize),
        IMPORT(sb4ReplaceFile),
        {"sb4HostPosixVersion", (Sb4CFunction)host_posix_version},
        {"sb4HostThreadWaitHandle", (Sb4CFunction)host_thread_wait_handle},
        {"sb4HostThreadSignaled", (Sb4CFunction)host_thread_signaled},
#endif
        IMPORT(isalpha), IMPORT(isdigit), IMPORT(isspace), IMPORT(isxdigit),
        IMPORT(malloc), IMPORT(memalign), IMPORT(memchr),
#ifdef __SWITCH2__
        {"memcmp", (Sb4CFunction)sb4CompareMemory},
        IMPORT(sb4CopyOpaqueRgba),
#else
        IMPORT(memcmp),
#endif
        IMPORT(memcpy), IMPORT(memmove), IMPORT(memrchr), IMPORT(memset),
        IMPORT(posix_memalign), IMPORT(qsort), IMPORT(realloc), IMPORT(roundf), IMPORT(snprintf), IMPORT(sprintf), IMPORT(sscanf),
        IMPORT(sb4StreamSubmitFrame), IMPORT(sb4StreamFrameAck), IMPORT(sb4StreamSetSourceReady),
        IMPORT(strcasecmp), IMPORT(strcasestr), IMPORT(strcat), IMPORT(strchr),
        IMPORT(strcmp), IMPORT(strcoll), IMPORT(strcpy), IMPORT(strcspn), IMPORT(strerror), IMPORT(strftime),
#ifdef __SWITCH2__
        IMPORT(strlcpy), IMPORT(strlcat),
#else
        {"strlcpy", (Sb4CFunction)strl_copy}, {"strlcat", (Sb4CFunction)strl_append},
#endif
        IMPORT(strlen), IMPORT(strncasecmp), IMPORT(strncmp), IMPORT(strncpy), IMPORT(strnlen),
        IMPORT(strpbrk), IMPORT(strrchr), IMPORT(strspn), IMPORT(strstr), IMPORT(strtok_r), IMPORT(strtod),
        IMPORT(strtof), IMPORT(strtok), IMPORT(strtol), IMPORT(strtoll), IMPORT(strtoul),
        IMPORT(strtoull), IMPORT(strxfrm), IMPORT(tolower), IMPORT(toupper), IMPORT(vsnprintf), IMPORT(vsprintf),
        IMPORT(mbrtowc), IMPORT(wcrtomb),
        IMPORT(acos), IMPORT(acosf), IMPORT(asin), IMPORT(asinf), IMPORT(atan), IMPORT(atanf),
        IMPORT(atan2), IMPORT(atan2f), IMPORT(cbrtf), IMPORT(ceil), IMPORT(ceilf),
        IMPORT(copysign), IMPORT(copysignf), IMPORT(cos), IMPORT(cosf), IMPORT(cosh), IMPORT(coshf),
        IMPORT(exp), IMPORT(exp2), IMPORT(exp2f), IMPORT(expf),
        IMPORT(fabs), IMPORT(fabsf), IMPORT(floor), IMPORT(floorf), IMPORT(fmaxf), IMPORT(fminf), IMPORT(fmod), IMPORT(fmodf),
        IMPORT(frexp), IMPORT(ldexp), IMPORT(log), IMPORT(log10), IMPORT(log10f), IMPORT(log2), IMPORT(log2f), IMPORT(logf),
        IMPORT(lround), IMPORT(lroundf), IMPORT(modf), IMPORT(modff), IMPORT(pow), IMPORT(powf), IMPORT(round),
        IMPORT(scalbn), IMPORT(scalbnf), IMPORT(sin), IMPORT(sinf), IMPORT(sinh), IMPORT(sinhf),
        IMPORT(sincos), IMPORT(sincosf), IMPORT(sqrt), IMPORT(sqrtf),
        IMPORT(tan), IMPORT(tanf), IMPORT(tanh), IMPORT(tanhf), IMPORT(trunc), IMPORT(truncf),
#undef IMPORT
#ifdef __SWITCH2__
#define SB4_POSIX(name, result, params, args, unavailable, provider) {#name, (Sb4CFunction)(provider)},
#include "host-posix.def"
#undef SB4_POSIX
#endif
    };
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); ++i)
        if (strcmp(name, functions[i].name) == 0) return functions[i].fn;
    return NULL;
}

static uint32_t allocate_aligned(void **out, size_t alignment, size_t bytes) {
    if (!out) return EINVAL;
    *out = NULL;
    if (!bytes || alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;
    return (uint32_t)posix_memalign(out, alignment, bytes);
}

static uint64_t ticks(void) { return armGetSystemTick(); }
static uint64_t tick_hz(void) { return armGetSystemTickFreq(); }
static void sleep_ns(uint64_t ns) {
    if (ns <= INT64_MAX) svcSleepThread((int64_t)ns);
}

const Sb4Compat *sb4GetCompat(void) {
    static const Sb4Compat api = {
        SB4_COMPAT_VERSION, sizeof(Sb4Compat), SB4_COMPAT_LIBOUNCE, 0,
        resolve_c, sb4ResolveService, allocate_aligned, free, ticks, tick_hz, sleep_ns, NULL
    };
    return &api;
}

#ifdef __SWITCH2__
static const Sb4Compat *application_base;
static Sb4Compat application_io;
static Sb4CFunction resolve_application_io(const char *name) {
    if (!name) return NULL;
#define SB4_APPLICATION_IO(fn) if (!strcmp(name, #fn)) return (Sb4CFunction)(fn);
#include "application-io.def"
#undef SB4_APPLICATION_IO
    return application_base->resolve_c(name);
}
const Sb4Compat *sb4GetApplicationIoCompat(const Sb4Compat *base) {
    if (!sb4CompatValid(base) || base->bytes < sizeof application_io) return base;
    application_base = base;
    application_io = *base;
    application_io.resolve_c = resolve_application_io;
    return &application_io;
}
#endif
