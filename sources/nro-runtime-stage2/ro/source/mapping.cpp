// SPDX-License-Identifier: GPL-2.0-only
//
// NRO mapping. RO maps the client's heap pages that hold the image (and its
// BSS) as code at a free address of the client's process, reads the header
// through a temporary view in RO's own address space, publishes the code to
// the instruction cache, and sets each segment's permissions.
//
// The address search follows stock RO: it skips four reserved regions of the
// process with a four-page margin, keeps the image's offset from the source
// within each alignment, tries 2 MiB, 64 KiB and then 4 KiB alignment, and
// gives up after 64 mapping attempts.
#include "image.h"
#include "session.h"

#include <cstring>
#include <switch/result.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/arm/cache.h>
}

namespace {

constexpr u64 PageSize = 0x1000;

/// Free pages that RO keeps before and after each image.
constexpr u64 GuardSize = 0x4000;

/// The alignments that the address search tries, largest first.
constexpr u64 SearchAlignments[] = {0x200000, 0x10000, 0x1000};

/// The number of mapping attempts, over all alignments, before
/// RoResult_OutOfAddressSpace.
constexpr u32 MaxMapAttempts = 64;

/// The regions that the address search skips, as svcGetInfo types of their
/// address and size. The last two are queried by stock RO; what they hold is
/// not identified.
struct ReservedRegionInfo {
    u32 address_type;
    u32 size_type;
};
constexpr ReservedRegionInfo ReservedRegionInfos[] = {
    {InfoType_AliasRegionAddress, InfoType_AliasRegionSize},
    {InfoType_HeapRegionAddress, InfoType_HeapRegionSize},
    {0x25, 0x26},
    {0x1d, 0x1e},
};
constexpr u32 ReservedRegionCount = sizeof(ReservedRegionInfos) / sizeof(ReservedRegionInfos[0]);

struct Region {
    u64 address;
    u64 size;
};

/// A process's address space and the regions in it that images avoid.
struct AddressSpace {
    u64 begin;
    u64 end;
    Region reserved[ReservedRegionCount];
};

//-----------------------------------------------------------------------------
// Address search
//-----------------------------------------------------------------------------

bool readAddressSpace(Handle process, AddressSpace* out) {
    u64 begin = 0;
    u64 size = 0;
    if (R_FAILED(svcGetInfo(&begin, InfoType_AslrRegionAddress, process, 0)) ||
        R_FAILED(svcGetInfo(&size, InfoType_AslrRegionSize, process, 0)) ||
        !size || begin > UINT64_MAX - size)
        return false;

    for (u32 i = 0; i < ReservedRegionCount; i++) {
        Region& region = out->reserved[i];
        if (R_FAILED(svcGetInfo(&region.address, ReservedRegionInfos[i].address_type, process, 0)) ||
            R_FAILED(svcGetInfo(&region.size, ReservedRegionInfos[i].size_type, process, 0)) ||
            region.address > UINT64_MAX - region.size)
            return false;
    }
    out->begin = begin;
    out->end = begin + size;
    return true;
}

Result queryMemory(Handle process, u64 address, MemoryInfo* info) {
    u32 page = 0;
    if (process == CUR_PROCESS_HANDLE)
        return svcQueryMemory(info, &page, address);
    return svcQueryProcessMemory(info, &page, process, address);
}

/// The first address at or after @p address that has the same offset from
/// @p source within @p alignment, or 0 when there is none.
u64 alignLike(u64 address, u64 source, u64 alignment) {
    const u64 delta = (source - address) & (alignment - 1);
    return address > UINT64_MAX - delta ? 0 : address + delta;
}

/// Whether [candidate, candidate + size) comes within @p guard of the region.
bool overlapsRegion(u64 candidate, u64 size, const Region& region, u64 guard) {
    if (!region.size)
        return false;
    if (region.address + region.size > UINT64_MAX - guard)
        return true;
    const u64 low = region.address > guard ? region.address - guard : 0;
    const u64 high = region.address + region.size + guard;
    return candidate < high && size <= UINT64_MAX - candidate && candidate + size > low;
}

/// The candidate itself when it clears the region, else the first address
/// past the region and its guard; 0 when that address does not exist.
u64 skipRegion(u64 candidate, u64 size, const Region& region, u64 guard) {
    if (!overlapsRegion(candidate, size, region, guard))
        return candidate;
    if (region.address + region.size > UINT64_MAX - guard)
        return 0;
    return region.address + region.size + guard;
}

/**
 * @brief Looks for an address for the image in one free block.
 * @param[out] out_address The address, or 0 when the block has none.
 * @return false when the search must stop: a reserved region ends at the top
 *         of the address space.
 */
bool findInFreeBlock(const AddressSpace& space, const MemoryInfo& block, u64 at, u64 size,
                     u64 source, u64 alignment, u64 guard, u64* out_address) {
    *out_address = 0;
    const u64 block_end = block.addr + block.size;
    if (block.addr > UINT64_MAX - guard || guard > block_end - block.addr)
        return true;

    const u64 low = at > block.addr + guard ? at : block.addr + guard;
    u64 high = block_end < space.end ? block_end : space.end;
    if (high < guard)
        return true;
    high -= guard;

    // Each pass moves the candidate past the reserved regions it touches. A
    // candidate that no region moves is the result.
    u64 candidate = alignLike(low, source, alignment);
    for (u32 pass = 0; pass <= ReservedRegionCount; pass++) {
        if (!candidate || candidate > high || size > high - candidate)
            return true;

        u64 next = candidate;
        for (const Region& region : space.reserved) {
            const u64 skipped = skipRegion(candidate, size, region, guard);
            if (!skipped)
                return false;
            if (skipped > next)
                next = skipped;
        }
        if (next == candidate) {
            *out_address = candidate;
            return true;
        }
        candidate = alignLike(next, source, alignment);
    }
    return true;
}

/**
 * @brief Finds a free address in the process for @p size bytes, at or after
 *        @p after, with @p guard free bytes on each side, outside the
 *        reserved regions, and with the same offset as @p source within
 *        @p alignment.
 *
 * The search queries each block from the start to the end of the address
 * space, however many the process has: a client with many images mapped has
 * several blocks for each one.
 * @return The address, or 0 when none was found.
 */
u64 findFreeAddress(Handle process, u64 size, u64 source, u64 alignment, u64 guard, u64 after) {
    AddressSpace space;
    if (!readAddressSpace(process, &space))
        return 0;

    u64 at = space.begin > after ? space.begin : after;
    if (!at)
        at = PageSize;

    while (at < space.end) {
        MemoryInfo block = {};
        if (R_FAILED(queryMemory(process, at, &block)) || !block.size ||
            block.addr > UINT64_MAX - block.size || block.addr + block.size <= at)
            return 0;

        if (block.type == MemType_Unmapped) {
            u64 address = 0;
            if (!findInFreeBlock(space, block, at, size, source, alignment, guard, &address))
                return 0;
            if (address)
                return address;
        }
        at = block.addr + block.size;
    }
    return 0;
}

//-----------------------------------------------------------------------------
// Checks on the client's memory
//-----------------------------------------------------------------------------

/**
 * @brief Checks that [source, source + size) is the client's own heap,
 *        read-write and not shared, which svcMapProcessCodeMemory needs.
 * @param[out] out_unsuitable Set when the range is not such memory, as
 *             opposed to a failed query.
 */
Result checkHeapRange(Handle process, u64 source, u64 size, bool* out_unsuitable) {
    *out_unsuitable = false;
    if (!size || source > UINT64_MAX - size) {
        *out_unsuitable = true;
        return RoResult_InvalidAddress;
    }

    const u64 end = source + size;
    for (u64 cursor = source; cursor < end;) {
        MemoryInfo info = {};
        const Result rc = queryMemory(process, cursor, &info);
        if (R_FAILED(rc))
            return rc;

        const bool heap = info.type == MemType_Heap && info.perm == Perm_Rw && !info.attr &&
                          !info.ipc_refcount && !info.device_refcount;
        const bool covers_cursor = info.size && info.addr <= cursor &&
                                   info.addr <= UINT64_MAX - info.size &&
                                   info.addr + info.size > cursor;
        if (!heap || !covers_cursor) {
            *out_unsuitable = true;
            return RoResult_InvalidAddress;
        }
        const u64 block_end = info.addr + info.size;
        cursor = block_end < end ? block_end : end;
    }
    return 0;
}

/**
 * @brief Translates a failed svcMapProcessCodeMemory as stock RO does.
 * @param[out] out_retry Set when the destination was taken: another address
 *             may work. The source was then checked again and is still heap.
 */
Result translateMapFailure(Handle process, Result rc, u64 source, u64 size, bool* out_retry) {
    *out_retry = false;
    if (R_VALUE(rc) == KERNELRESULT(ResourceExhausted))
        return RoResult_OsOutOfMemory;
    if (R_VALUE(rc) != KERNELRESULT(InvalidMemoryState))
        return rc;

    bool unsuitable = false;
    const Result source_rc = checkHeapRange(process, source, size, &unsuitable);
    if (R_FAILED(source_rc))
        return unsuitable ? RoResult_OsInvalidCurrentMemoryState : source_rc;

    *out_retry = true;
    return rc;
}

/// Whether [address, address + size) is free in the process.
Result isRangeFree(Handle process, u64 address, u64 size, bool* out_free) {
    MemoryInfo info = {};
    const Result rc = queryMemory(process, address, &info);
    if (R_FAILED(rc))
        return rc;
    if (!info.size || info.addr > UINT64_MAX - info.size)
        return RoResult_InvalidAddress;

    const bool contains = address >= info.addr && size <= info.size &&
                          address - info.addr <= info.size - size;
    *out_free = info.type == MemType_Unmapped && contains;
    return 0;
}

//-----------------------------------------------------------------------------
// Mapping and unmapping
//-----------------------------------------------------------------------------

/// Unmaps what the record says is mapped, latest first. A failure stops
/// there and leaves the rest recorded.
Result unmapRecorded(Handle process, RoLoadedImage* record) {
    if (record->mapped & RoImageState_AliasMapped) {
        const Result rc = svcUnmapProcessMemory(reinterpret_cast<void*>(record->alias), process,
                                                record->base, record->size);
        if (R_FAILED(rc))
            return rc;
        record->mapped &= ~RoImageState_AliasMapped;
        record->alias = 0;
    }
    if (record->mapped & RoImageState_BssMapped) {
        const Result rc = svcUnmapProcessCodeMemory(process, record->base + record->size,
                                                    record->bss_source, record->bss_size);
        if (R_FAILED(rc))
            return rc;
        record->mapped &= ~RoImageState_BssMapped;
    }
    if (record->mapped & RoImageState_CodeMapped) {
        const Result rc = svcUnmapProcessCodeMemory(process, record->base, record->source,
                                                    record->size);
        if (R_FAILED(rc))
            return rc;
        record->mapped &= ~RoImageState_CodeMapped;
    }
    record->base = 0;
    return 0;
}

/// Unmaps what failed to load, and returns the failure, or the cleanup's
/// failure when the cleanup fails too.
Result rollBack(Handle process, RoLoadedImage* record, Result failure) {
    const Result rc = unmapRecorded(process, record);
    return R_FAILED(rc) ? rc : failure;
}

bool checkLoadArguments(const RoImageRequest& request) {
    const u64 page_mask = PageSize - 1;
    if (!request.source || !request.size || ((request.source | request.size | request.bss_size) & page_mask))
        return false;
    if (request.bss_size && (!request.bss_source || (request.bss_source & page_mask)))
        return false;
    return request.source + request.size >= request.source &&
           request.bss_source + request.bss_size >= request.bss_source &&
           request.size + request.bss_size >= request.size;
}

/**
 * @brief Maps the image and BSS at @p candidate and checks that the guard
 *        pages around them are free. On failure, unmaps them again.
 * @param[out] out_retry Set when another address may work: the destination
 *             was taken, or a guard page is not free.
 */
Result mapAtAddress(Handle process, const RoImageRequest& request, u64 candidate,
                    RoLoadedImage* record, bool* out_retry) {
    *out_retry = false;
    Result rc = svcMapProcessCodeMemory(process, candidate, request.source, request.size);
    if (R_FAILED(rc))
        return translateMapFailure(process, rc, request.source, request.size, out_retry);

    record->base = candidate;
    record->source = request.source;
    record->size = request.size;
    record->bss_source = request.bss_source;
    record->bss_size = request.bss_size;
    record->mapped = RoImageState_CodeMapped;

    if (request.bss_size) {
        rc = svcMapProcessCodeMemory(process, candidate + request.size, request.bss_source,
                                     request.bss_size);
        if (R_FAILED(rc))
            rc = translateMapFailure(process, rc, request.bss_source, request.bss_size, out_retry);
        else
            record->mapped |= RoImageState_BssMapped;
    }

    if (R_SUCCEEDED(rc)) {
        bool leading_free = false;
        bool trailing_free = false;
        rc = isRangeFree(process, candidate - GuardSize, GuardSize, &leading_free);
        if (R_SUCCEEDED(rc) && leading_free)
            rc = isRangeFree(process, candidate + request.size + request.bss_size, GuardSize, &trailing_free);
        if (R_SUCCEEDED(rc) && (!leading_free || !trailing_free)) {
            rc = RoResult_OutOfAddressSpace;
            *out_retry = true;
        }
    }
    if (R_SUCCEEDED(rc))
        return 0;

    const Result cleanup_rc = unmapRecorded(process, record);
    if (R_FAILED(cleanup_rc)) {
        *out_retry = false;
        return cleanup_rc;
    }
    return rc;
}

/// Maps the image at the first free address that works, trying each
/// alignment in turn.
Result mapAtFreeAddress(Handle process, const RoImageRequest& request, RoLoadedImage* record,
                        u64* out_destination) {
    u32 attempts = 0;
    for (const u64 alignment : SearchAlignments) {
        u64 after = 0;
        while (attempts < MaxMapAttempts) {
            const u64 candidate = findFreeAddress(process, request.size + request.bss_size,
                                                  request.source, alignment, GuardSize, after);
            if (!candidate)
                break;
            attempts++;

            bool retry = false;
            const Result rc = mapAtAddress(process, request, candidate, record, &retry);
            if (R_SUCCEEDED(rc)) {
                *out_destination = candidate;
                return 0;
            }
            if (!retry)
                return rc;

            if (candidate > UINT64_MAX - alignment)
                break;
            after = candidate + alignment;
        }
        if (attempts >= MaxMapAttempts)
            break;
    }
    return RoResult_OutOfAddressSpace;
}

bool isLoadedElsewhere(const u8 module_id[32], const RoLoadedImage* record,
                       const RoLoadedImage* existing, u32 existing_count) {
    for (u32 i = 0; i < existing_count; i++) {
        if (&existing[i] != record && existing[i].mapped &&
            !std::memcmp(existing[i].module_id, module_id, sizeof(existing[i].module_id)))
            return true;
    }
    return false;
}

/**
 * @brief Reads the mapped image through a view in RO: checks its layout and
 *        that the module is not already loaded, and publishes the code to the
 *        instruction cache. The view is unmapped again on success.
 */
Result inspectMappedImage(Handle process, u64 destination, const RoImageRequest& request,
                          RoLoadedImage* record, const RoLoadedImage* existing, u32 existing_count,
                          RoImageLayout* out_layout) {
    const u64 local = findFreeAddress(CUR_PROCESS_HANDLE, request.size, 0, PageSize, 0, 0);
    if (!local)
        return RoResult_InvalidAddress;

    void* view = reinterpret_cast<void*>(local);
    Result rc = svcMapProcessMemory(view, process, destination, request.size);
    if (R_FAILED(rc))
        return rc;
    record->alias = local;
    record->mapped |= RoImageState_AliasMapped;

    const u8* header = static_cast<const u8*>(view);
    if (!roGetImageLayout(header, request.size, request.bss_size, out_layout)) {
        roRecordImageRejection(header, request.size, request.bss_size);
        return RoResult_InvalidNro;
    }
    if (isLoadedElsewhere(out_layout->module_id, record, existing, existing_count))
        return RoResult_AlreadyLoaded;
    std::memcpy(record->module_id, out_layout->module_id, sizeof(record->module_id));

    armDCacheClean(view, request.size);
    armICacheInvalidate(view, request.size);

    rc = svcUnmapProcessMemory(view, process, destination, request.size);
    if (R_FAILED(rc))
        return rc;
    record->mapped &= ~RoImageState_AliasMapped;
    record->alias = 0;
    return 0;
}

/// Sets the segment permissions: text RX (its header page R with
/// RoNroFlag_ReadOnlyHeader), ro R, data and BSS RW.
Result applyPermissions(Handle process, u64 destination, const RoImageLayout& layout, u64 bss_size) {
    const u64 header_size = (layout.flags & RoNroFlag_ReadOnlyHeader) ? PageSize : 0;
    const u64 ro_address = destination + layout.text_size;
    const u64 data_address = ro_address + layout.ro_size;

    Result rc = 0;
    if (header_size)
        rc = svcSetProcessMemoryPermission(process, destination, header_size, Perm_R);
    if (R_SUCCEEDED(rc) && layout.text_size > header_size)
        rc = svcSetProcessMemoryPermission(process, destination + header_size,
                                           layout.text_size - header_size, Perm_Rx);
    if (R_SUCCEEDED(rc))
        rc = svcSetProcessMemoryPermission(process, ro_address, layout.ro_size, Perm_R);
    if (R_SUCCEEDED(rc))
        rc = svcSetProcessMemoryPermission(process, data_address,
                                           static_cast<u64>(layout.data_size) + bss_size, Perm_Rw);
    return rc;
}

/// Whether the source and BSS pages are the client's heap.
Result checkSourcePages(Handle process, const RoImageRequest& request) {
    bool unsuitable = false;
    Result rc = checkHeapRange(process, request.source, request.size, &unsuitable);
    if (R_SUCCEEDED(rc) && request.bss_size)
        rc = checkHeapRange(process, request.bss_source, request.bss_size, &unsuitable);
    return rc;
}

} // namespace

Result roUnmapImage(Handle process, RoLoadedImage* record) {
    if (!record->base)
        return RoResult_NotLoaded;
    record->mapped &= ~RoImageState_Ready;
    return unmapRecorded(process, record);
}

Result roMapImage(Handle process, const RoImageRequest& request, RoLoadedImage* record,
                  const RoLoadedImage* existing, u32 existing_count, u64* out_base) {
    if (!checkLoadArguments(request) || record->mapped)
        return RoResult_InvalidAddress;

    Result rc = checkSourcePages(process, request);
    if (R_FAILED(rc))
        return rc;

    u64 destination = 0;
    rc = mapAtFreeAddress(process, request, record, &destination);
    if (R_FAILED(rc))
        return rc;

    RoImageLayout layout = {};
    rc = inspectMappedImage(process, destination, request, record, existing, existing_count, &layout);
    if (R_SUCCEEDED(rc))
        rc = applyPermissions(process, destination, layout, request.bss_size);
    if (R_FAILED(rc))
        return rollBack(process, record, rc);

    record->mapped |= RoImageState_Ready;
    *out_base = destination;
    return 0;
}
