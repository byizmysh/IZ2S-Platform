#ifndef PS1_NATIVE_KEEPAWAKE_H
#define PS1_NATIVE_KEEPAWAKE_H
#include <stdint.h>
#include <stddef.h>
/* Optional, caller-owned state outside the resettable image. The caller
 * qualifies the member function and keeps its controller alive until join.
 * This record owns neither the controller nor any SDK resource. */
typedef struct {
 uint64_t fn,controller,last_tick,interval_ticks,calls,failures;
 uint32_t last_result,enabled;
 uint64_t last_call_tick;
} Ps1KeepAwake;
_Static_assert(sizeof(Ps1KeepAwake)==64,"native keepawake ABI");
_Static_assert(offsetof(Ps1KeepAwake,fn)==0,"keepawake function ABI");
_Static_assert(offsetof(Ps1KeepAwake,controller)==8,"keepawake member receiver ABI");
_Static_assert(offsetof(Ps1KeepAwake,last_tick)==16,"keepawake observed clock ABI");
_Static_assert(offsetof(Ps1KeepAwake,interval_ticks)==24,"keepawake cadence ABI");
_Static_assert(offsetof(Ps1KeepAwake,calls)==32,"keepawake invocation counter ABI");
_Static_assert(offsetof(Ps1KeepAwake,failures)==40,"keepawake failure counter ABI");
_Static_assert(offsetof(Ps1KeepAwake,last_result)==48,"keepawake Result ABI");
_Static_assert(offsetof(Ps1KeepAwake,enabled)==52,"keepawake enabled ABI");
_Static_assert(offsetof(Ps1KeepAwake,last_call_tick)==56,"keepawake scheduling ABI");
/* interval_ticks is normally 30 * tick_hz, strictly below 2^63. Clock wrap
 * uses modulo arithmetic; a backward jump rebases the interval without a
 * spurious invocation. Nonzero Result disables this optional facility.
 * UINT32_MAX records invalid local configuration, without making an SDK call. */
void ps1_keepawake_step(Ps1KeepAwake *,uint64_t tick);
#endif
