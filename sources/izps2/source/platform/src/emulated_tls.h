#ifndef PS2_EMULATED_TLS_H
#define PS2_EMULATED_TLS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_TLS_CONTROLS 512u
#define PS2_TLS_DTORS 256u
/* Clang/GCC emulated TLS descriptor ABI; all target code uses -femulated-tls. */
typedef struct {
    uintptr_t size, alignment;
    union { uintptr_t index; void *address; } object;
    void *initial_value;
} Ps2TlsControl;
typedef struct {
    uint32_t (*allocate_slot)(uint32_t *,void (*)(uintptr_t));
    void (*free_slot)(uint32_t);
    uintptr_t (*get_value)(uint32_t);
    void (*set_value)(uint32_t,uintptr_t);
    void *(*allocate)(size_t,size_t);
    void (*release)(void *);
    void (*fatal)(uint32_t); /* must not return */
} Ps2TlsApi;
typedef struct {
    uint32_t ready,slot,controls,threads,last_error;
    uint64_t allocations,releases,destructors;
} Ps2TlsStatistics;
/* SDK callbacks use one allocated nn::os TLS slot. No TP register is replaced.
 * Initialize/shutdown only while workers are quiescent. Detach the calling
 * thread before shutdown, and keep descriptors loaded until shutdown returns.
 * A thread must not access compiled C++ TLS again after detaching: the compiler
 * may cache its address for the thread's lifetime. Restart with fresh workers.
 * Callbacks must not recursively access TLS owned by this runtime. */
int ps2_tls_initialize(const Ps2TlsApi *);
int ps2_tls_shutdown(void);
void ps2_tls_detach_current(void);
void ps2_tls_statistics(Ps2TlsStatistics *);
void *__emutls_get_address(Ps2TlsControl *);
int __cxa_thread_atexit(void (*)(void *),void *,void *);
#ifdef __cplusplus
}
#endif
#endif
