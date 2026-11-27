// SPDX-License-Identifier: GPL-2.0-only
//
// NRO mapping (source/mapping.cpp) against a mocked kernel: address search
// with reserved regions, mapping retries, guard pages, permissions, the
// duplicate-module check, and rollback when a step or its cleanup fails.
#include "../source/session.h"
#include "switch/result.h"
extern "C" {
#include "switch/kernel/svc.h"
#include "switch/arm/cache.h"
}
#include <assert.h>
#include <cstring>

namespace {

alignas(4096) u8 g_local_image[0x3000];
constexpr Handle TargetProcess = 42;
constexpr u64 Source = 0x20000000;
constexpr u64 BssSource = 0x30000000;
constexpr u64 Destination = 0x10000000;
constexpr Result QueryFailure = 0xf001;

u64 g_mapped_destination = Destination;
u64 g_reserved_alias, g_reserved_alias_size;
u64 g_reserved_extra, g_reserved_extra_size;
u32 g_reserved_extra_type;
Handle g_reserved_extra_process;
u32 g_map_calls, g_unmap_calls, g_permission_calls, g_alias_unmaps;
u32 g_fail_map_call, g_fail_permission_call;
bool g_fail_alias_unmap;
Result g_fail_map_result;
bool g_fail_every_map, g_source_invalid_after_fail, g_source_query_error_after_fail;
u32 g_block_guard_once, g_block_trailing_once, g_guard_query_error_once, g_guard_checks;
bool g_split_source;
u64 g_fail_unmap_source;
u64 g_unmapped_source[4];
/// The number of one-page blocks in use at the start of the target's address
/// space: a client that has many images mapped.
u32 g_occupied_blocks;

u64 localImage() {
    return reinterpret_cast<u64>(g_local_image);
}

void write32(u32 offset, u32 value) {
    std::memcpy(g_local_image + offset, &value, sizeof(value));
}

void resetCounters() {
    g_map_calls = g_unmap_calls = g_permission_calls = g_alias_unmaps = 0;
    g_fail_map_call = g_fail_permission_call = 0;
    g_fail_alias_unmap = false;
    g_fail_map_result = KERNELRESULT(ResourceExhausted);
    g_fail_every_map = g_source_invalid_after_fail = g_source_query_error_after_fail = false;
    g_block_guard_once = g_block_trailing_once = g_guard_query_error_once = g_guard_checks = 0;
    g_split_source = false;
    g_fail_unmap_source = 0;
    g_mapped_destination = Destination;
    g_reserved_alias = g_reserved_alias_size = 0;
    g_reserved_extra = g_reserved_extra_size = 0;
    g_reserved_extra_type = 0;
    g_reserved_extra_process = TargetProcess;
    g_occupied_blocks = 0;
    std::memset(g_unmapped_source, 0, sizeof(g_unmapped_source));
}

} // namespace

Result svcGetInfo(u64* out, u32 type, Handle process, u64 subtype) {
    (void)subtype;
    const bool self = process == CUR_PROCESS_HANDLE;
    *out = 0;
    if (type == InfoType_AslrRegionAddress)
        *out = self ? localImage() : Destination - 0x100000;
    else if (type == InfoType_AslrRegionSize)
        *out = self ? 0x100000 : 0x400000;
    else if (type == InfoType_AliasRegionAddress && !self)
        *out = g_reserved_alias;
    else if (type == InfoType_AliasRegionSize && !self)
        *out = g_reserved_alias_size;
    else if (type == g_reserved_extra_type && process == g_reserved_extra_process)
        *out = g_reserved_extra;
    else if (type == g_reserved_extra_type + 1 && process == g_reserved_extra_process)
        *out = g_reserved_extra_size;
    return 0;
}

Result svcQueryMemory(MemoryInfo* info, u32* page, u64 address) {
    assert(address == localImage());
    *info = MemoryInfo{};
    info->addr = address;
    info->size = 0x100000;
    *page = 0;
    return 0;
}

Result svcQueryProcessMemory(MemoryInfo* info, u32* page, Handle process, u64 address) {
    assert(process == TargetProcess);
    *page = 0;
    *info = MemoryInfo{};
    if (address == Source || address == BssSource || (g_split_source && address == Source + 0x1000)) {
        if (g_source_query_error_after_fail && g_map_calls)
            return QueryFailure;
        info->addr = address;
        info->size = g_split_source && address == Source ? 0x1000 : 0x3000;
        info->type = MemType_Heap;
        info->perm = Perm_Rw;
        if (g_source_invalid_after_fail && g_map_calls)
            info->perm = Perm_R;
        return 0;
    }

    assert(address >= Destination - 0x100000 && address < Destination + 0x300000);
    const u64 occupied_end = Destination - 0x100000 + u64{g_occupied_blocks} * 0x1000;
    if (address < occupied_end) {
        info->addr = address & ~u64{0xfff};
        info->size = 0x1000;
        info->type = MemType_CodeStatic;
        info->perm = Perm_Rx;
        return 0;
    }
    info->addr = occupied_end;
    info->size = Destination + 0x300000 - occupied_end;
    const bool guard = address == g_mapped_destination - 0x4000 ||
                       address == g_mapped_destination + 0x4000;
    if (guard && g_map_calls >= 2) {
        ++g_guard_checks;
        if (g_guard_query_error_once) {
            --g_guard_query_error_once;
            return QueryFailure;
        }
        if (g_block_guard_once) {
            --g_block_guard_once;
            info->type = MemType_Heap;
        }
        if (g_block_trailing_once && address == g_mapped_destination + 0x4000) {
            --g_block_trailing_once;
            info->type = MemType_Heap;
        }
    }
    return 0;
}

Result svcMapProcessMemory(void* local, Handle process, u64 remote, u64 size) {
    assert(local == g_local_image && process == TargetProcess);
    assert(remote == g_mapped_destination && size == 0x3000);
    return 0;
}

Result svcUnmapProcessMemory(void* local, Handle process, u64 remote, u64 size) {
    assert(local == g_local_image && process == TargetProcess);
    assert(remote == g_mapped_destination && size == 0x3000);
    ++g_alias_unmaps;
    return g_fail_alias_unmap ? KERNELRESULT(InvalidMemoryState) : 0;
}

Result svcMapProcessCodeMemory(Handle process, u64 at, u64 from, u64 size) {
    assert(process == TargetProcess);
    assert((from == Source && size == 0x3000) || (from == BssSource && size == 0x1000));
    ++g_map_calls;
    if (from == Source)
        g_mapped_destination = at;
    else
        assert(at == g_mapped_destination + 0x3000);
    return (g_fail_every_map || g_map_calls == g_fail_map_call) ? g_fail_map_result : 0;
}

Result svcUnmapProcessCodeMemory(Handle process, u64 at, u64 from, u64 size) {
    assert(process == TargetProcess);
    assert((from == Source && at == g_mapped_destination && size == 0x3000) ||
           (from == BssSource && at == g_mapped_destination + 0x3000 && size == 0x1000));
    g_unmapped_source[g_unmap_calls++] = from;
    return from == g_fail_unmap_source ? KERNELRESULT(InvalidMemoryState) : 0;
}

Result svcSetProcessMemoryPermission(Handle process, u64 at, u64 size, u32 permission) {
    assert(process == TargetProcess && size);
    assert((at == g_mapped_destination && permission == Perm_Rx) ||
           (at == g_mapped_destination + 0x1000 && permission == Perm_R) ||
           (at == g_mapped_destination + 0x2000 && permission == Perm_Rw));
    ++g_permission_calls;
    return g_permission_calls == g_fail_permission_call ? KERNELRESULT(InvalidMemoryState) : 0;
}

void armDCacheClean(void* address, size_t size) {
    assert(address == g_local_image && size == 0x3000);
}

void armICacheInvalidate(void* address, size_t size) {
    assert(address == g_local_image && size == 0x3000);
}

void roRecordImageRejection(const void*, u64, u64) {
}

int main() {
    std::memset(g_local_image, 0, sizeof(g_local_image));
    write32(0x10, 0x304f524e);
    write32(0x18, 0x3000);
    write32(0x24, 0x1000);
    write32(0x28, 0x1000);
    write32(0x2c, 0x1000);
    write32(0x30, 0x2000);
    write32(0x34, 0x1000);
    write32(0x38, 0x1000);
    const RoImageRequest args = {Source, 0x3000, BssSource, 0x1000};
    RoLoadedImage record = {};
    u64 base = 0;

    // A successful map, then unmap in reverse order.
    resetCounters();
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(base == Destination && record.base == Destination);
    assert(g_map_calls == 2 && g_permission_calls == 3 && g_alias_unmaps == 1);
    assert(roUnmapImage(TargetProcess, &record) == 0);
    assert(!record.base && g_unmap_calls == 2);
    assert(g_unmapped_source[0] == BssSource && g_unmapped_source[1] == Source);

    // The kernel is out of resources: OS OutOfMemory, after rolling back.
    resetCounters();
    g_fail_map_call = 2;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0x1203);
    assert(g_map_calls == 2 && g_unmap_calls == 1 && g_unmapped_source[0] == Source);
    assert(!record.base);

    resetCounters();
    g_fail_map_call = 1;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0x1203);
    assert(g_map_calls == 1 && !record.base);

    // Any other kernel failure is returned as it is.
    resetCounters();
    g_fail_map_call = 1;
    g_fail_map_result = 0xdc01;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0xdc01);
    assert(g_map_calls == 1 && !record.base);

    // An invalid memory state with a valid source is a collision: try the next address.
    resetCounters();
    g_fail_map_call = 1;
    g_fail_map_result = KERNELRESULT(InvalidMemoryState);
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(g_map_calls == 3 && base == Destination + 0x200000);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    // ... unless the source is no longer the caller's heap.
    resetCounters();
    g_fail_map_call = 1;
    g_fail_map_result = KERNELRESULT(InvalidMemoryState);
    g_source_invalid_after_fail = true;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0x40203);
    assert(g_map_calls == 1 && !record.base);

    resetCounters();
    g_fail_map_call = 1;
    g_fail_map_result = KERNELRESULT(InvalidMemoryState);
    g_source_query_error_after_fail = true;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == QueryFailure);
    assert(g_map_calls == 1 && !record.base);

    // A source made of two heap blocks is still valid.
    resetCounters();
    g_split_source = true;
    g_fail_map_call = 1;
    g_fail_map_result = KERNELRESULT(InvalidMemoryState);
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(g_map_calls == 3 && base == Destination + 0x200000);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    // The search has no fixed number of blocks: a client with 300 blocks in
    // use before the first free address gets the address after them.
    resetCounters();
    g_occupied_blocks = 300;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(base == Destination + 0x200000 && g_map_calls == 2);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    // Retries stop after 64 attempts.
    resetCounters();
    g_fail_every_map = true;
    g_fail_map_result = KERNELRESULT(InvalidMemoryState);
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == RoResult_OutOfAddressSpace);
    assert(g_map_calls == 64 && !record.base);

    // A colliding BSS mapping rolls back the code and retries.
    resetCounters();
    g_fail_map_call = 2;
    g_fail_map_result = KERNELRESULT(InvalidMemoryState);
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(g_map_calls == 4 && g_unmap_calls == 1 && base == Destination + 0x200000);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    // An occupied leading or trailing guard page moves the image.
    resetCounters();
    g_block_guard_once = 1;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(g_guard_checks >= 2 && g_unmap_calls == 2 && base == Destination + 0x200000);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    resetCounters();
    g_block_trailing_once = 1;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(g_guard_checks >= 3 && g_unmap_calls == 2 && base == Destination + 0x200000);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    resetCounters();
    g_guard_query_error_once = 1;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == QueryFailure);
    assert(g_map_calls == 2 && g_unmap_calls == 2 && !record.base);

    // A failed rollback keeps the record of what is still mapped.
    resetCounters();
    g_block_guard_once = 1;
    g_fail_unmap_source = BssSource;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == KERNELRESULT(InvalidMemoryState));
    assert(g_map_calls == 2 && record.base == Destination &&
           (record.mapped & (RoImageState_CodeMapped | RoImageState_BssMapped)) ==
               (RoImageState_CodeMapped | RoImageState_BssMapped));
    g_fail_unmap_source = 0;
    assert(roUnmapImage(TargetProcess, &record) == 0);
    assert(!record.base && !record.mapped);

    resetCounters();
    g_fail_map_call = 2;
    g_fail_map_result = KERNELRESULT(InvalidMemoryState);
    g_fail_unmap_source = Source;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == KERNELRESULT(InvalidMemoryState));
    assert(g_map_calls == 2 && record.base == Destination && (record.mapped & RoImageState_CodeMapped));
    g_fail_unmap_source = 0;
    assert(roUnmapImage(TargetProcess, &record) == 0);
    assert(!record.base && !record.mapped);

    // A failed permission change unmaps everything.
    resetCounters();
    g_fail_permission_call = 2;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == KERNELRESULT(InvalidMemoryState));
    assert(g_unmap_calls == 2 && g_unmapped_source[0] == BssSource);
    assert(g_unmapped_source[1] == Source && !record.base);

    // A failed alias unmap keeps the image mapped and recorded.
    resetCounters();
    g_fail_alias_unmap = true;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == KERNELRESULT(InvalidMemoryState));
    assert(record.base == Destination && (record.mapped & RoImageState_AliasMapped) && g_unmap_calls == 0);
    g_fail_alias_unmap = false;
    assert(roUnmapImage(TargetProcess, &record) == 0);
    assert(!record.base && !record.mapped && g_unmap_calls == 2);

    // A module that is already loaded is rejected before any permission change.
    resetCounters();
    RoLoadedImage records[2] = {};
    records[0].base = Destination + 0x8000;
    records[0].mapped = RoImageState_Ready | RoImageState_CodeMapped;
    assert(roMapImage(TargetProcess, args, &records[1], records, 2, &base) == RoResult_AlreadyLoaded);
    assert(g_permission_calls == 0 && g_unmap_calls == 2 && !records[1].mapped);

    // Reserved regions of the process, with their guard margins, are skipped.
    resetCounters();
    g_reserved_alias = Destination;
    g_reserved_alias_size = 0x10000;
    g_mapped_destination = Destination + 0x200000;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(base == g_mapped_destination && record.base == g_mapped_destination);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    resetCounters();
    g_reserved_extra_type = 0x25;
    g_reserved_extra = Destination;
    g_reserved_extra_size = 0x10000;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(base == Destination + 0x200000);
    assert(roUnmapImage(TargetProcess, &record) == 0);

    // No room in RO's own address space for the alias: InvalidAddress.
    resetCounters();
    g_reserved_extra_type = 0x25;
    g_reserved_extra_process = CUR_PROCESS_HANDLE;
    g_reserved_extra = localImage();
    g_reserved_extra_size = 0x100000;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0x80216);
    assert(g_map_calls == 2 && g_unmap_calls == 2 && !record.base);

    resetCounters();
    g_reserved_extra_type = 0x1d;
    g_reserved_extra = Destination;
    g_reserved_extra_size = 0x10000;
    assert(roMapImage(TargetProcess, args, &record, &record, 1, &base) == 0);
    assert(base == Destination + 0x200000);
    assert(roUnmapImage(TargetProcess, &record) == 0);
    return 0;
}
