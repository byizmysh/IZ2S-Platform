#ifndef PS2_CODE_ARENA_H
#define PS2_CODE_ARENA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_PAGE 4096u
#define PS2_MAX_PAGES 131072u
/* All calls belong to one emulation thread. No page may execute during publication.
 * map/unmap return 0 on success. Every error is treated as uncertain ownership. */
typedef int (*Ps2PageOperation)(void *, uintptr_t, uintptr_t);
typedef void (*Ps2CacheOperation)(uintptr_t, size_t);
typedef struct {
    uint8_t *source, *shadow, *rx;
    size_t bytes;
    void *opaque;
    Ps2PageOperation map, unmap;
    Ps2CacheOperation clean, invalidate;
    uint32_t pages, live_pages, peak_pages;
    uint32_t poisoned, last_error;
    uint64_t allocations, releases, publications, maps, unmaps;
    /* 0: free; head: allocation page count; continuation: UINT32_MAX. */
    uint32_t runs[PS2_MAX_PAGES];
    /* Per-page head+1 gives constant-time subrange lookup in the 305-MiB core
     * cache; walking all continuation pages on each emitter call is too slow. */
    uint32_t heads[PS2_MAX_PAGES];
    /* Lazy reservations own VA runs without publishing untouched RX pages. */
    uint8_t mapped[PS2_MAX_PAGES];
    uint32_t mapped_pages;
} Ps2CodeArena;
int ps2_arena_init(Ps2CodeArena *, void *, void *, void *, size_t,
                   void *, Ps2PageOperation, Ps2PageOperation,
                   Ps2CacheOperation, Ps2CacheOperation);
int ps2_arena_allocate(Ps2CodeArena *, const void *, size_t, void **);
int ps2_arena_reserve_zero(Ps2CodeArena *, size_t, void **);
int ps2_arena_reserve_lazy(Ps2CodeArena *, size_t, void **);
int ps2_arena_publish(Ps2CodeArena *, void *, size_t);
int ps2_arena_release(Ps2CodeArena *, void *, size_t);
void *ps2_arena_writable(Ps2CodeArena *, const void *, size_t);
/* Subranges support ARMSX2's persistent buffers and in-place branch patching. */
int ps2_arena_publish_range(Ps2CodeArena *, void *, size_t);
void *ps2_arena_writable_range(Ps2CodeArena *, const void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
