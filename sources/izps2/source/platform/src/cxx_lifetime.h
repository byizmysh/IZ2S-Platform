#ifndef PS2_CXX_LIFETIME_H
#define PS2_CXX_LIFETIME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    /* Callbacks must not throw. lock/unlock protect the complete registry.
     * Allocation/free and destructors run outside that lock and may reenter.
     * Each allocation must satisfy the requested alignment. */
    void (*lock)(void);
    void (*unlock)(void);
    void *(*allocate)(size_t,size_t);
    void (*release)(void *);
    void (*fatal)(uint32_t);
    uint64_t (*self)(void); /* stable nonzero identity of the calling thread */
    void (*wait)(void); /* atomically releases/reacquires the registry lock */
    void (*wake_all)(void); /* called with that lock held */
} Ps2CxxLifetimeApi;
typedef struct {
    uint64_t registered,invoked,pending,inflight,allocated,released;
} Ps2CxxLifetimeStatistics;
/* Bind after allocator/thread setup, before C++ constructors. Initialization
 * and shutdown require quiescent callers. Keep this code, all callback code
 * and each DSO's registered functions resident through its finalization.
 * Finalization removes a callback before invoking it, so recursive/concurrent
 * finalization cannot invoke it twice. Finalizers are serialized; same-thread
 * recursive finalization is allowed. Selection and invocation follow reverse
 * registration order. A callback may register more callbacks or recursively
 * finalize, but must not wait for a different thread's finalization to finish.
 * Callers must stop a DSO's workers/registrations before unloading that DSO.
 * Finalization is not safe in a signal/fault handler. */
int ps2_cxx_lifetime_initialize(const Ps2CxxLifetimeApi *);
int ps2_cxx_lifetime_shutdown(void);
void ps2_cxx_lifetime_statistics(Ps2CxxLifetimeStatistics *);
#ifdef __cplusplus
}
#endif
#endif
