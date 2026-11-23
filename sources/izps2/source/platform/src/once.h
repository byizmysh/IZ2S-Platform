#ifndef PS2_ONCE_H
#define PS2_ONCE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    /* wait may return spuriously, but must coordinate its value check with
     * wake_all to prevent lost wakeups. Callbacks must not throw. */
    void (*wait)(uint32_t *address,uint32_t expected);
    void (*wake_all)(uint32_t *address);
    void (*fatal)(uint32_t code);
} Ps2OnceApi;
typedef struct { uint64_t executions,completed,rolled_back,waits; } Ps2OnceStatistics;
/* Bind before any C++ constructors, while all workers are quiescent. */
int ps2_once_initialize(const Ps2OnceApi *);
int ps2_once_run(uint32_t *control,void (*initialize)(void));
void ps2_once_statistics(Ps2OnceStatistics *);
#ifdef __cplusplus
}
#endif
#endif
