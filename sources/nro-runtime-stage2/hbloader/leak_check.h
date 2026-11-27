/* SPDX-License-Identifier: ISC
 * What an NRO left behind in the loader's process.
 *
 * The loader takes a snapshot of its process before the first NRO runs and
 * another after each NRO returns and is unloaded, and compares them. A
 * snapshot holds the process's memory map (every block's address, size,
 * type, attributes, permissions and reference counts), the number of free
 * thread slots and the memory in use. After a clean return the two are the
 * same: the NRO's heap is plain heap again, and no thread, stack, TLS page,
 * mapping or code memory of the NRO remains.
 *
 * The game's policy does not allow the resource-limit syscalls, so open
 * sessions and events are not counted here.
 */
#ifndef NX_HBLOADER_SWITCH2_LEAK_CHECK_H
#define NX_HBLOADER_SWITCH2_LEAK_CHECK_H

#include <stdbool.h>
#include <stdint.h>

enum {
    HBLOADER_SNAPSHOT_BLOCKS = 512,
};

/* One block of the memory map, as svcQueryMemory describes it. */
typedef struct HbloaderMemoryBlock {
    uint64_t address;
    uint64_t size;
    uint32_t type;
    uint32_t attributes;
    uint32_t permission;
    uint32_t ipc_references;
    uint32_t device_references;
} HbloaderMemoryBlock;

/* The kernel queries that a snapshot uses. Each returns 0 or a failure. */
typedef struct HbloaderProcessQueries {
    void *context;
    uint32_t (*query_memory)(void *context, uint64_t address, HbloaderMemoryBlock *out);
    uint32_t (*free_thread_count)(void *context, uint64_t *out);
    uint32_t (*used_memory_size)(void *context, uint64_t *out);
} HbloaderProcessQueries;

typedef struct HbloaderSnapshot {
    bool has_free_threads;
    bool has_used_memory;
    bool map_complete; /* false: a query failed or the map has more blocks than fit */
    uint64_t free_threads;
    uint64_t used_memory;
    uint32_t block_count;
    HbloaderMemoryBlock blocks[HBLOADER_SNAPSHOT_BLOCKS];
} HbloaderSnapshot;

/* What changed between two snapshots. */
typedef struct HbloaderLeakReport {
    bool threads_compared;
    bool memory_compared;
    bool map_compared;
    int64_t threads;        /* thread slots in use now that were free before */
    int64_t memory;         /* bytes in use now that were not before */
    uint32_t changed_blocks; /* blocks of the new map not in the old one */
    bool has_changed_block;
    HbloaderMemoryBlock first_changed_block;
} HbloaderLeakReport;

/* Takes a snapshot. The range [excluded_begin, excluded_end) is the loader's
 * own memory: blocks are cut at its edges and the part inside is left out,
 * so the loader's own use of its heap does not appear as a change. Adjacent
 * blocks that are the same in all but address are recorded as one. */
void hbloaderTakeSnapshot(const HbloaderProcessQueries *queries, uint64_t excluded_begin,
                          uint64_t excluded_end, HbloaderSnapshot *out);

void hbloaderCompareSnapshots(const HbloaderSnapshot *before, const HbloaderSnapshot *after,
                              HbloaderLeakReport *out);

/* Whether the report shows anything left behind. */
bool hbloaderLeakFound(const HbloaderLeakReport *report);

#endif
