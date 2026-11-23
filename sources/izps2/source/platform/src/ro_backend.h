#ifndef PS2_RO_BACKEND_H
#define PS2_RO_BACKEND_H
#include "code_arena.h"
typedef struct {
    uint64_t pid, ro_pid;
    uint32_t service;
    uint32_t (*send)(void *, uint64_t, uint32_t);
    uintptr_t source, rx;
    size_t bytes;
    uint32_t transport, service_result;
} Ps2RoBackend;
/* Uses the existing, already opened endpoint. It does not acquire new privileges. */
int ps2_ro_map_page(void *, uintptr_t, uintptr_t);
int ps2_ro_unmap_page(void *, uintptr_t, uintptr_t);
void ps2_ro_clean(uintptr_t, size_t);
void ps2_ro_invalidate(uintptr_t, size_t);
#endif
