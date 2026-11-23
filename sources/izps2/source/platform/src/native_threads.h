#ifndef PS2_NATIVE_THREADS_H
#define PS2_NATIVE_THREADS_H
#include "sdk_threads.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Pinned SDK pthread profile: attr 56, scalar attrs 4, mutex/condition 8,
 * rwlock 48. SDK sem_t is larger than the port's 24-byte inline capacity;
 * use an owned composite of two 8-byte SDK objects and two 32-bit counters.
 * The caller verifies its SDK fingerprints before supplying these callbacks.
 * Code/imports remain resident until all workers are joined. Detached workers
 * are disabled. No TP register or Linux pthread object crosses this boundary. */
/* Composite sem_post uses an SDK mutex: ordinary thread context only, not an
 * async-signal-safe POSIX implementation. Never call it from a fault handler. */
uint32_t ps2_native_threads_initialize(const Ps2SdkThreadApi *);
typedef struct {uint64_t semaphore_waits,semaphore_blocks,semaphore_posts;} Ps2NativeThreadStatistics;
void ps2_native_threads_statistics(Ps2NativeThreadStatistics *);
#ifdef __cplusplus
}
#endif
#endif
