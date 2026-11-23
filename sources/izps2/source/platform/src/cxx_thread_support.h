#ifndef PS2_CXX_THREAD_SUPPORT_H
#define PS2_CXX_THREAD_SUPPORT_H
#include "sdk_threads.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Bind before constructors or workers. Each callback must have its live SDK
 * fingerprint checked by the owner. The SDK timespec is two signed 64-bit
 * words. Callbacks and this image remain resident through all joined workers.
 * This adapter supplies the three extra libc ABI imports used by GCC 15
 * std::thread: get_nprocs, nanosleep and __errno_location. No Linux ABI calls
 * or thread-pointer registers are used here. */
typedef struct {
    uint64_t (*available_core_mask)(void);
    int (*nanosleep)(const Ps2SdkTimespec *,Ps2SdkTimespec *);
    int *(*errno_location)(void);
} Ps2CxxThreadApi;
typedef struct {
    uint64_t initial_core_mask,core_queries,sleep_calls,errno_queries;
} Ps2CxxThreadStatistics;
int ps2_cxx_thread_initialize(const Ps2CxxThreadApi *);
void ps2_cxx_thread_statistics(Ps2CxxThreadStatistics *);
#ifdef __cplusplus
}
#endif
#endif
