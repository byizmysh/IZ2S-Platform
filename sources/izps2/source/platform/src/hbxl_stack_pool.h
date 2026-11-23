#ifndef PS2_HBXL_STACK_POOL_H
#define PS2_HBXL_STACK_POOL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Ps2HbxlMemorySnapshot {
    uint64_t sdk_reserved, sdk_used, sdk_largest_free, vk_reserved, vk_used;
    uint64_t sdk_failed_requests, sdk_last_request;
    int32_t sdk_last_result;
    uint32_t sdk_chunks, stack_scopes, poisoned;
} Ps2HbxlMemorySnapshot;
/* Consistent read-only observation under the allocator's existing lock. */
void hbxl_memory_snapshot(Ps2HbxlMemorySnapshot *out);
/* Bracket the synchronous libc pthread_create call on its creating thread.
 * All mmap allocations made by that caller (stack, TLS and clone metadata)
 * must use SDK memory; other callers retain access to the large cached pool. */
int hbxl_stack_scope_enter(void);
int hbxl_stack_scope_leave(void);
void *hbxl_mmap_sdk(size_t bytes);
uint32_t hbxl_stack_scopes_live(void);
#ifdef __cplusplus
}
#endif
#endif
