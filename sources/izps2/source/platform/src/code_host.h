#ifndef PS2_CODE_HOST_H
#define PS2_CODE_HOST_H
#include "code_arena.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The frontend must stop/exclude every other JIT executor before returning
 * from enter_publication. It must never throw. Code frames on this thread may
 * be suspended below the C call; pages are republished before returning.
 * current_thread is a stable nonzero SDK thread identity, not a libc object.
 * These callbacks and all backing memory must outlive the bound core. */
typedef struct {
    void *opaque;
    uint64_t (*current_thread)(void *);
    int (*enter_publication)(void *);
    void (*leave_publication)(void *);
    /* Optional finer exclusion: the range is page-rounded. If supplied,
     * exclude every other executor that can reach these pages. Independent
     * immutable execution domains may keep running. No additional writers
     * may access the arena until leave_publication. Release still uses the
     * full enter_publication callback after all executors are stopped. */
    int (*enter_publication_range)(void *,uintptr_t,size_t);
} Ps2CodeHostApi;
enum {PS2_CODE_INVALID=0xf501,PS2_CODE_BUSY=0xf502,PS2_CODE_POISONED=0xf503};
int ps2_code_host_bind(Ps2CodeArena *,const Ps2CodeHostApi *);
int ps2_code_host_bound(void);
int ps2_code_host_reserve(size_t,void **);
int ps2_code_host_release(void);
/* Single writer, nested scopes. End may publish a smaller prefix than begin
 * (ARMSX2 trampolines do this); it must have the same starting address. */
int ps2_code_host_begin(void *,size_t);
int ps2_code_host_end(void *,size_t);
int ps2_code_host_begin_all(void);
int ps2_code_host_end_all(void);
void *ps2_code_host_writable(const void *,size_t);
/* ARMSX2 also reads its shadow through armGetWritableCodePtr outside a scope.
 * Translation alone grants no publication; emitted code still needs end(). */
void *ps2_code_host_translate(const void *,size_t);
#ifdef __cplusplus
}
#endif
#endif
