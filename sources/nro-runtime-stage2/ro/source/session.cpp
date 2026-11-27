// SPDX-License-Identifier: GPL-2.0-only
// RO's ldr:ro client contexts: NRR records, NRO loading, the JIT commands
// and the homebrew session courier.
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/kernel/virtmem.h>
#include <switch/arm/cache.h>
}
#include <cstdlib>
#include <cstring>
#include "switch/sf/hipc.h"
#include "switch/sf/cmif.h"
#include "control.h"
#include "session.h"

namespace {

constexpr u64 PageMask = 0xfff;

RoDiagnostics g_ro_diagnostics = {};

/// What a command returns besides its result. Each part is sent only when
/// the command succeeds.
struct CommandReply {
    u64 value = 0;
    bool has_value = false;
    Handle handle = INVALID_HANDLE; ///< Copied to the client; RO closes its own reference after sending.
    KaihenJitAliasInfo alias = {};
    bool has_alias = false;
};

void recordRequest(const RoSession* session, void* base) {
    const HipcParsedRequest request = hipcParseRequest(base);
    g_ro_diagnostics.ldr_requests++;
    g_ro_diagnostics.last_port = session->port_index;
    g_ro_diagnostics.last_type = request.meta.type;
    g_ro_diagnostics.last_data_words = request.meta.num_data_words;
    g_ro_diagnostics.last_copy_handles = request.meta.num_copy_handles;
    g_ro_diagnostics.last_move_handles = request.meta.num_move_handles;
    g_ro_diagnostics.last_command = UINT32_MAX;
    std::memcpy(g_ro_diagnostics.last_request, base, sizeof(g_ro_diagnostics.last_request));

    const u8* end = reinterpret_cast<const u8*>(request.data.data_words + request.meta.num_data_words);
    const auto* header = static_cast<const CmifInHeader*>(cmifGetAlignedDataStart(request.data.data_words, base));
    if (end <= static_cast<const u8*>(base) + 0x100 && reinterpret_cast<const u8*>(header + 1) <= end &&
        header->magic == CMIF_IN_HEADER_MAGIC)
        g_ro_diagnostics.last_command = header->command_id;
}

/// Closes the handles that a request carried. Ownership comes from the HIPC
/// header, even when the decoder rejects the command.
void closeRequestHandles(void* base) {
    const HipcParsedRequest request = hipcParseRequest(base);
    for (u32 i = 0; i < request.meta.num_copy_handles; i++) {
        if (request.data.copy_handles[i])
            svcCloseHandle(request.data.copy_handles[i]);
    }
    for (u32 i = 0; i < request.meta.num_move_handles; i++) {
        if (request.data.move_handles[i])
            svcCloseHandle(request.data.move_handles[i]);
    }
}

bool isPageRange(u64 address, u64 size) {
    return address && size && !((address | size) & PageMask) && address + size > address;
}

//-----------------------------------------------------------------------------
// Initialize and NRR/NRO
//-----------------------------------------------------------------------------

/// Binds the client context to the sender's process. The copied handle must
/// be the sender's, and no other context may hold that process.
Result initialize(RoSession* sessions, u32 num_sessions, u32 index, const RoRequest& request) {
    RoSession* s = &sessions[index];
    u64 pid = 0;
    Result rc = svcGetProcessId(&pid, request.process);
    if (R_SUCCEEDED(rc) && pid != request.pid)
        rc = RoResult_BadRequest;

    // A context initialized again keeps its handle; the new copy is closed.
    if (R_SUCCEEDED(rc) && s->process_handle) {
        svcCloseHandle(request.process);
        return s->process_id == pid ? 0 : RoResult_BadRequest;
    }
    for (u32 i = 0; R_SUCCEEDED(rc) && i < num_sessions; i++) {
        if (i != index && sessions[i].process_handle && sessions[i].process_id == pid)
            rc = RoResult_BadRequest;
    }
    if (R_FAILED(rc)) {
        svcCloseHandle(request.process);
        return rc;
    }

    s->process_id = pid;
    s->process_handle = request.process;
    return 0;
}

Result loadNrr(RoSession* s, u64 source, u64 size) {
    if (!source || !size || ((source | size) & PageMask) || source + size < source)
        return RoResult_BadRequest;

    u32 free_slot = RoMaxNrr;
    for (u32 i = 0; i < RoMaxNrr; i++) {
        if (s->nrr_size[i] && s->nrr_source[i] == source)
            return RoResult_BadRequest;
        if (!s->nrr_size[i] && free_slot == RoMaxNrr)
            free_slot = i;
    }
    if (free_slot == RoMaxNrr)
        return RoResult_TooManyNrr;

    s->nrr_source[free_slot] = source;
    s->nrr_size[free_slot] = size;
    return 0;
}

Result unloadNrr(RoSession* s, u64 source) {
    for (u32 i = 0; i < RoMaxNrr; i++) {
        if (s->nrr_size[i] && s->nrr_source[i] == source) {
            s->nrr_source[i] = 0;
            s->nrr_size[i] = 0;
            return 0;
        }
    }
    return RoResult_NotRegistered;
}

/// Finds a free NRO record of the context. When all are in use, the array
/// grows to twice its size. Returns nullptr when RO has no memory for that.
RoLoadedImage* acquireNroRecord(RoSession* s) {
    for (u32 i = 0; i < s->nro_capacity; i++) {
        if (!s->nro[i].mapped)
            return &s->nro[i];
    }

    constexpr u32 InitialCapacity = 8;
    const u32 capacity = s->nro_capacity ? s->nro_capacity * 2 : InitialCapacity;
    auto* grown = static_cast<RoLoadedImage*>(std::realloc(s->nro, capacity * sizeof(RoLoadedImage)));
    if (!grown)
        return nullptr;
    std::memset(grown + s->nro_capacity, 0, (capacity - s->nro_capacity) * sizeof(RoLoadedImage));

    RoLoadedImage* record = grown + s->nro_capacity;
    s->nro = grown;
    s->nro_capacity = capacity;
    return record;
}

Result loadNro(RoSession* s, const RoRequest& request, CommandReply* reply) {
    const RoImageRequest image = {request.args[0], request.args[1], request.args[2], request.args[3]};
    RoLoadedImage* record = acquireNroRecord(s);
    if (!record)
        return RoResult_TooManyNro;

    const Result rc = roMapImage(s->process_handle, image, record, s->nro, s->nro_capacity, &reply->value);
    reply->has_value = true;
    return rc;
}

Result unloadNro(RoSession* s, u64 base) {
    for (u32 i = 0; i < s->nro_capacity; i++) {
        if ((s->nro[i].mapped & RoImageState_Ready) && s->nro[i].base == base)
            return roUnmapImage(s->process_handle, &s->nro[i]);
    }
    return RoResult_NotLoaded;
}

/// Unloads all NROs of the context, and what a failed load left mapped. An
/// image that cannot be unmapped stays recorded; the reply value counts the
/// images removed, and the result is the last failure.
Result releaseNros(RoSession* s, CommandReply* reply) {
    Result last_failure = 0;
    u64 count = 0;
    for (u32 i = 0; i < s->nro_capacity; i++) {
        RoLoadedImage& image = s->nro[i];
        if (!image.mapped)
            continue;
        const Result rc = roUnmapImage(s->process_handle, &image);
        if (R_SUCCEEDED(rc))
            count++;
        else
            last_failure = rc;
    }
    reply->value = count;
    reply->has_value = true;
    return last_failure;
}

//-----------------------------------------------------------------------------
// JIT code
//-----------------------------------------------------------------------------

RoJitMapping* findJitMapping(RoSession* s, u64 destination, u64 size) {
    for (RoJitMapping& entry : s->jit) {
        if (entry.size && entry.destination == destination && entry.size == size)
            return &entry;
    }
    return nullptr;
}

RoJitMapping* findJitAlias(RoSession* s, u64 destination) {
    for (RoJitMapping& entry : s->jit) {
        if (entry.destination == destination && entry.alias)
            return &entry;
    }
    return nullptr;
}

/// Maps the process's own pages at @p source to @p destination as RX code.
/// The source pages stay inaccessible until the mapping is unmapped.
Result mapJitCode(RoSession* s, u64 destination, u64 source, u64 size) {
    if (!isPageRange(destination, size) || !isPageRange(source, size))
        return RoResult_BadRequest;

    RoJitMapping* entry = nullptr;
    for (RoJitMapping& candidate : s->jit) {
        if (!candidate.size) {
            entry = &candidate;
            break;
        }
    }
    if (!entry)
        return RoResult_OutOfAddressSpace;

    Result rc = svcMapProcessCodeMemory(s->process_handle, destination, source, size);
    if (R_FAILED(rc))
        return rc;
    rc = svcSetProcessMemoryPermission(s->process_handle, destination, size, Perm_Rx);
    if (R_FAILED(rc)) {
        svcUnmapProcessCodeMemory(s->process_handle, destination, source, size);
        return rc;
    }

    *entry = RoJitMapping{destination, source, size, nullptr};
    return 0;
}

/// Maps a writable view of a JIT mapping into RO and keeps it, so that the
/// client can publish code into it with PublishJitAlias and PublishJitRanges.
Result mapJitAlias(RoSession* s, u64 destination, u64 size, CommandReply* reply) {
    if (!isPageRange(destination, size))
        return RoResult_BadRequest;
    RoJitMapping* entry = findJitMapping(s, destination, size);
    if (!entry || entry->alias)
        return RoResult_BadRequest;

    virtmemLock();
    void* local = virtmemFindAslr(size, 0x1000);
    if (!local) {
        virtmemUnlock();
        return RoResult_OutOfAddressSpace;
    }
    Result rc = svcMapProcessMemory(local, s->process_handle, destination, size);
    virtmemUnlock();
    if (R_FAILED(rc))
        return rc;
    entry->alias = local;

    // The view must be writable; otherwise it is of no use and is removed.
    MemoryInfo info = {};
    u32 page = 0;
    rc = svcQueryMemory(&info, &page, reinterpret_cast<u64>(local));
    if (R_SUCCEEDED(rc) && !(info.perm & Perm_W))
        rc = RoResult_BadRequest;
    if (R_FAILED(rc)) {
        const Result unmap_rc = svcUnmapProcessMemory(local, s->process_handle, destination, size);
        if (R_FAILED(unmap_rc))
            return unmap_rc;
        entry->alias = nullptr;
        return rc;
    }

    reply->alias = KaihenJitAliasInfo{reinterpret_cast<u64>(local), info.type, info.perm, info.attr, 0};
    reply->has_alias = true;
    return 0;
}

void publish(void* target, const void* bytes, u64 size) {
    std::memcpy(target, bytes, size);
    armDCacheFlush(target, size);
}

Result publishJitAlias(RoSession* s, u64 destination, u64 offset, u64 size, const RoRequest& request) {
    if (!request.input || !size || size != request.input_size)
        return RoResult_BadRequest;
    RoJitMapping* entry = findJitAlias(s, destination);
    if (!entry || offset > entry->size || size > entry->size - offset)
        return RoResult_BadRequest;

    publish(static_cast<u8*>(entry->alias) + offset, request.input, size);
    return 0;
}

/// Copies several ranges into a JIT view. All ranges are checked before any
/// is written: each lies in the mapping, and together they use exactly the
/// payload.
Result publishJitRanges(RoSession* s, u64 destination, u64 count, const RoRequest& request) {
    if (!request.ranges || !request.input || !count ||
        count > request.ranges_size / sizeof(KaihenJitRange) ||
        request.ranges_size != count * sizeof(KaihenJitRange))
        return RoResult_BadRequest;
    RoJitMapping* entry = findJitAlias(s, destination);
    if (!entry)
        return RoResult_BadRequest;

    const auto* ranges = static_cast<const KaihenJitRange*>(request.ranges);
    u64 payload_size = 0;
    for (u64 i = 0; i < count; i++) {
        const KaihenJitRange range = ranges[i];
        if (!range.size || range.offset > entry->size || range.size > entry->size - range.offset ||
            range.size > request.input_size - payload_size)
            return RoResult_BadRequest;
        payload_size += range.size;
    }
    if (payload_size != request.input_size)
        return RoResult_BadRequest;

    const auto* payload = static_cast<const u8*>(request.input);
    for (u64 i = 0; i < count; i++) {
        const KaihenJitRange range = ranges[i];
        publish(static_cast<u8*>(entry->alias) + range.offset, payload, range.size);
        payload += range.size;
    }
    return 0;
}

/// Unmaps RO's view of a JIT mapping, if any, then the mapping.
Result unmapJitEntry(RoSession* s, RoJitMapping* entry) {
    if (entry->alias) {
        const Result rc = svcUnmapProcessMemory(entry->alias, s->process_handle, entry->destination, entry->size);
        if (R_FAILED(rc))
            return rc;
        entry->alias = nullptr;
    }
    const Result rc = svcUnmapProcessCodeMemory(s->process_handle, entry->destination, entry->source, entry->size);
    if (R_SUCCEEDED(rc))
        *entry = RoJitMapping{};
    return rc;
}

/// Unmaps a mapping that mapJitCode made, so the request cannot unmap an NRO.
Result unmapJitCode(RoSession* s, u64 destination, u64 source, u64 size) {
    RoJitMapping* entry = findJitMapping(s, destination, size);
    if (!entry || entry->source != source)
        return RoResult_BadRequest;
    return unmapJitEntry(s, entry);
}

/// Unmaps all JIT code of the context. An entry that fails stays recorded;
/// the reply value counts the mappings removed, and the result is the last
/// failure.
Result releaseJitCode(RoSession* s, CommandReply* reply) {
    Result last_failure = 0;
    u64 count = 0;
    for (RoJitMapping& entry : s->jit) {
        if (!entry.size)
            continue;
        const Result rc = unmapJitEntry(s, &entry);
        if (R_SUCCEEDED(rc))
            count++;
        else
            last_failure = rc;
    }
    reply->value = count;
    reply->has_value = true;
    return last_failure;
}

//-----------------------------------------------------------------------------
// Dispatch
//-----------------------------------------------------------------------------

/// Runs a command of an initialized client context.
Result runClientCommand(RoSession* s, const RoRequest& request, CommandReply* reply) {
    const u64* args = request.args;
    switch (request.cmd_id) {
    case RoCmd_LoadNro:
        return loadNro(s, request, reply);
    case RoCmd_UnloadNro:
        return unloadNro(s, args[0]);
    case RoCmd_LoadNrr:
        return loadNrr(s, args[0], args[1]);
    case RoCmd_UnloadNrr:
        return unloadNrr(s, args[0]);
    case RoCmd_GetHomebrewSession:
        return roControlCreateHomebrewSession(s->process_handle, &reply->handle);
    case RoCmd_MapJitCode:
        return mapJitCode(s, args[0], args[1], args[2]);
    case RoCmd_UnmapJitCode:
        return unmapJitCode(s, args[0], args[1], args[2]);
    case RoCmd_ReleaseJitCode:
        return releaseJitCode(s, reply);
    case RoCmd_MapJitAlias:
        return mapJitAlias(s, args[0], args[1], reply);
    case RoCmd_PublishJitAlias:
        return publishJitAlias(s, args[0], args[1], args[2], request);
    case RoCmd_PublishJitRanges:
        return publishJitRanges(s, args[0], args[1], request);
    case RoCmd_ReleaseNros:
        return releaseNros(s, reply);
    default:
        return RoResult_BadRequest;
    }
}

Result runCommand(RoSession* sessions, u32 num_sessions, u32 index, const RoRequest& request,
                  CommandReply* reply) {
    RoSession* s = &sessions[index];
    if (request.cmd_id == RoCmd_ProvisionHenControl) {
        reply->has_value = true;
        return roControlAdopt(request.process, &reply->value);
    }
    if (request.cmd_id == RoCmd_Initialize)
        return initialize(sessions, num_sessions, index, request);
    if (!s->process_handle || s->process_id != request.pid)
        return RoResult_BadRequest;
    return runClientCommand(s, request, reply);
}

void writeCommandReply(void* base, Result rc, const CommandReply& reply) {
    if (reply.handle != INVALID_HANDLE)
        roWriteHandleReply(base, rc, reply.handle);
    else if (reply.has_alias)
        roWriteJitAliasReply(base, rc, &reply.alias);
    else
        roWriteReply(base, rc, reply.value, reply.has_value);
}

} // namespace

void roRecordAcceptedSession(u32 port_index) {
    if (port_index == 0)
        g_ro_diagnostics.accepted_ldr_set1++;
    else if (port_index == 1)
        g_ro_diagnostics.accepted_ldr_set2++;
    else if (port_index == 2)
        g_ro_diagnostics.accepted_dmnt++;
}

void roRecordImageRejection(const void* header, u64 image_size, u64 bss_size) {
    g_ro_diagnostics.image_rejections++;
    g_ro_diagnostics.last_image_size = image_size;
    g_ro_diagnostics.last_bss_size = bss_size;
    std::memcpy(g_ro_diagnostics.last_image_header, header, sizeof(g_ro_diagnostics.last_image_header));
}

RoDiagnostics roGetDiagnostics() {
    return g_ro_diagnostics;
}

Result roSessionClose(RoSession* s) {
    // The process has usually exited. The kernel frees its memory only when
    // the last handle to it closes, so a failed unmap must not keep RO's
    // handle: it is closed below in every case. RO cannot remove a view that
    // failed without the handle; that view stays mapped in RO.
    Result last_failure = 0;
    for (RoJitMapping& entry : s->jit) {
        if (!entry.alias)
            continue;
        const Result rc = svcUnmapProcessMemory(entry.alias, s->process_handle, entry.destination, entry.size);
        if (R_FAILED(rc))
            last_failure = rc;
        entry.alias = nullptr;
    }
    for (u32 i = 0; i < s->nro_capacity; i++) {
        RoLoadedImage& image = s->nro[i];
        if (image.mapped && !(image.mapped & RoImageState_Ready)) {
            const Result rc = roUnmapImage(s->process_handle, &image);
            if (R_FAILED(rc))
                last_failure = rc;
        }
    }
    if (R_FAILED(last_failure)) {
        g_ro_diagnostics.cleanup_failures++;
        g_ro_diagnostics.last_cleanup_result = last_failure;
    }

    if (s->process_handle)
        svcCloseHandle(s->process_handle);
    s->process_handle = 0;
    s->process_id = 0;

    // As in stock RO's teardown, loaded NROs stay mapped: the process may
    // still be running them.
    for (u32 i = 0; i < RoMaxNrr; i++) {
        s->nrr_source[i] = 0;
        s->nrr_size[i] = 0;
    }
    std::free(s->nro);
    s->nro = nullptr;
    s->nro_capacity = 0;
    for (RoJitMapping& entry : s->jit)
        entry = RoJitMapping{};
    return last_failure;
}

Handle roDispatch(RoSession* sessions, u32 num_sessions, u32 index, void* base) {
    recordRequest(&sessions[index], base);
    if (roReplyQueryPointerBufferSize(base))
        return INVALID_HANDLE;

    RoRequest request;
    if (!roParseRequest(base, &request)) {
        g_ro_diagnostics.rejected_requests++;
        g_ro_diagnostics.last_result = RoResult_BadRequest;
        closeRequestHandles(base);
        roWriteReply(base, RoResult_BadRequest, 0, false);
        return INVALID_HANDLE;
    }

    g_ro_diagnostics.parsed_requests++;
    CommandReply reply;
    const Result rc = runCommand(sessions, num_sessions, index, request, &reply);
    g_ro_diagnostics.last_result = rc;
    writeCommandReply(base, rc, reply);
    return reply.handle;
}
