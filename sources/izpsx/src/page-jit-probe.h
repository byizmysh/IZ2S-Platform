#ifndef PS1_PAGE_JIT_PROBE_H
#define PS1_PAGE_JIT_PROBE_H
#include "platform.h"
typedef struct {
 Ps1Context cpu;
 uint64_t loops,executions,first,last,begin,end,unmaps,maps;
 uint64_t callback_error,sdk_send,next_value,stage;
} Ps1PageJitProbe;
_Static_assert(sizeof(Ps1PageJitProbe)==608,"page JIT probe ABI");
#endif
