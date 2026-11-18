#ifndef SB4_HOST_HEAP_H
#define SB4_HOST_HEAP_H

#include "compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* For an engine image linked against SB4::HostedHeap. Bind once before
 * its constructors and before starting any threads. The launcher owns the
 * allocator through teardown. Allocations may cross the module boundary
 * when both sides use this allocator. The optional heapGetFreeSize import
 * reports launcher allocator capacity. Hosts without it report zero. */
uint32_t sb4HostHeapBind(const Sb4Compat *compat);

#ifdef __cplusplus
}
#endif
#endif
