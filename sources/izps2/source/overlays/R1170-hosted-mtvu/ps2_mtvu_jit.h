#pragma once

// The VM owner configures this before CPU providers allocate the primary JIT
// arena. The MTVU thread is joined before the owner releases that arena.
void iz_track_mtvu_jit(bool enabled);
void iz_mtvu_worker_enter();
void iz_mtvu_worker_publish_handoff();
void iz_mtvu_worker_leave();
bool iz_is_mtvu_worker();
