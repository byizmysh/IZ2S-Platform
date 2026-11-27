/* SPDX-License-Identifier: ISC */
#include "leak_check.h"

#include <stddef.h>

/* The kernel's memory type of unmapped address space. */
static const uint32_t MemoryTypeUnmapped = 0;

static bool sameProperties(const HbloaderMemoryBlock *a, const HbloaderMemoryBlock *b) {
    return a->type == b->type && a->attributes == b->attributes && a->permission == b->permission &&
           a->ipc_references == b->ipc_references && a->device_references == b->device_references;
}

static bool sameBlock(const HbloaderMemoryBlock *a, const HbloaderMemoryBlock *b) {
    return a->address == b->address && a->size == b->size && sameProperties(a, b);
}

/* Adds [begin, end) of a block, merging it into the previous one when they
 * touch and have the same properties. Returns false when the snapshot is full. */
static bool addBlock(HbloaderSnapshot *snapshot, const HbloaderMemoryBlock *block, uint64_t begin,
                     uint64_t end) {
    if (begin >= end)
        return true;

    if (snapshot->block_count) {
        HbloaderMemoryBlock *last = &snapshot->blocks[snapshot->block_count - 1];
        if (last->address + last->size == begin && sameProperties(last, block)) {
            last->size += end - begin;
            return true;
        }
    }
    if (snapshot->block_count == HBLOADER_SNAPSHOT_BLOCKS)
        return false;

    HbloaderMemoryBlock *added = &snapshot->blocks[snapshot->block_count++];
    *added = *block;
    added->address = begin;
    added->size = end - begin;
    return true;
}

/* Adds the parts of a block outside the excluded range. */
static bool addOutsideExcluded(HbloaderSnapshot *snapshot, const HbloaderMemoryBlock *block,
                               uint64_t excluded_begin, uint64_t excluded_end) {
    const uint64_t begin = block->address;
    const uint64_t end = block->address + block->size;
    const uint64_t before_end = end < excluded_begin ? end : excluded_begin;
    const uint64_t after_begin = begin > excluded_end ? begin : excluded_end;
    return addBlock(snapshot, block, begin, before_end) &&
           addBlock(snapshot, block, after_begin, end);
}

static void readMemoryMap(const HbloaderProcessQueries *queries, uint64_t excluded_begin,
                          uint64_t excluded_end, HbloaderSnapshot *out) {
    out->map_complete = false;
    uint64_t address = 0;
    for (;;) {
        HbloaderMemoryBlock block = {0};
        if (queries->query_memory(queries->context, address, &block) || !block.size ||
            block.address > address)
            return;
        /* The last block reaches the end of the address space. */
        const bool last = block.address + block.size <= address || block.address + block.size == 0;
        if (last)
            block.size = UINT64_MAX - block.address;
        if (!addOutsideExcluded(out, &block, excluded_begin, excluded_end))
            return;
        if (last)
            break;
        address = block.address + block.size;
    }
    out->map_complete = true;
}

void hbloaderTakeSnapshot(const HbloaderProcessQueries *queries, uint64_t excluded_begin,
                          uint64_t excluded_end, HbloaderSnapshot *out) {
    out->block_count = 0;
    out->has_free_threads = !queries->free_thread_count(queries->context, &out->free_threads);
    out->has_used_memory = !queries->used_memory_size(queries->context, &out->used_memory);
    readMemoryMap(queries, excluded_begin, excluded_end, out);
}

static bool containsBlock(const HbloaderSnapshot *snapshot, const HbloaderMemoryBlock *block) {
    for (uint32_t i = 0; i < snapshot->block_count; i++) {
        if (sameBlock(&snapshot->blocks[i], block))
            return true;
    }
    return false;
}

void hbloaderCompareSnapshots(const HbloaderSnapshot *before, const HbloaderSnapshot *after,
                              HbloaderLeakReport *out) {
    *out = (HbloaderLeakReport){0};

    out->threads_compared = before->has_free_threads && after->has_free_threads;
    if (out->threads_compared)
        out->threads = (int64_t)(before->free_threads - after->free_threads);

    out->memory_compared = before->has_used_memory && after->has_used_memory;
    if (out->memory_compared)
        out->memory = (int64_t)(after->used_memory - before->used_memory);

    out->map_compared = before->map_complete && after->map_complete;
    if (!out->map_compared)
        return;
    for (uint32_t i = 0; i < after->block_count; i++) {
        const HbloaderMemoryBlock *block = &after->blocks[i];
        if (containsBlock(before, block))
            continue;
        out->changed_blocks++;
        /* The first changed block that is mapped says the most: an unmapped
         * gap only changed because a mapping next to it did. */
        const bool better = !out->has_changed_block ||
                            (out->first_changed_block.type == MemoryTypeUnmapped &&
                             block->type != MemoryTypeUnmapped);
        if (better) {
            out->first_changed_block = *block;
            out->has_changed_block = true;
        }
    }
}

bool hbloaderLeakFound(const HbloaderLeakReport *report) {
    return report->threads > 0 || report->memory > 0 || report->changed_blocks > 0;
}
