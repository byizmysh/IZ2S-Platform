#ifndef PS2_HEAP_PAGES_H
#define PS2_HEAP_PAGES_H
#include "sdk_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Fallback for processes without VAM. A reservation physically owns its entire
 * SDK heap block until release. Commit/decommit are logical page operations:
 * commit restores RW and zeroes; decommit removes access but does not return
 * physical memory. protect(None -> RW) preserves data. No executable pages.
 * Allocate/release/protect callbacks use verified SDK scalar ABIs. The SDK's
 * protection function is void and aborts on failure; its return is not Result.
 * Mutations and binding require quiescent memory users and a single owner. */
typedef struct {
    int (*allocate)(void **,size_t,size_t);
    void (*release)(void *);
    void (*protect)(uintptr_t,size_t,unsigned);
    void (*fatal)(uint32_t); /* must not return */
} Ps2HeapPagesApi;
typedef struct {uintptr_t base;size_t bytes;} Ps2HeapBlock;
typedef struct {
    Ps2HeapPagesApi api;
    Ps2HeapBlock blocks[PS2_MEMORY_REGIONS];
    size_t limit_bytes,live_bytes;
    uint32_t ready,active,poisoned,last_error;
    uint64_t allocations,releases,commits,decommits,protections;
} Ps2HeapPages;
enum {PS2_HEAP_INVALID=0xf601,PS2_HEAP_BUSY=0xf602,PS2_HEAP_LIMIT=0xf603,PS2_HEAP_BAD_REPLY=0xf604};
/* Zero-initialized provider; only one process-local provider is bound. Keep
 * this storage and callbacks live until every region is released and unbind
 * returns. The returned memory API's enabled() means heap-provider readiness,
 * never the SDK VAM capability. Keep it bound while a Ps2SdkMemory uses it. */
uint32_t ps2_heap_pages_bind(Ps2HeapPages *,const Ps2HeapPagesApi *,size_t);
uint32_t ps2_heap_pages_unbind(Ps2HeapPages *);
const Ps2SdkMemoryApi *ps2_heap_pages_memory_api(void);
#ifdef __cplusplus
}
#endif
#endif
