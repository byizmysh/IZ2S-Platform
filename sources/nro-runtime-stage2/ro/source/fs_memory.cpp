// SPDX-License-Identifier: GPL-2.0-only
#include "fs_memory.h"

#include <cstring>
#include <switch.h>
#include "../../common/ro_dmnt.h"
#include "session.h"
#include "wire.h"

namespace {

constexpr u64 FilesystemProgramId = 0x0400000000000000ULL;
constexpr u64 PageSize = 0x1000;
constexpr u64 MaximumReadSize = 0x400000;
constexpr u64 MaximumWriteSize = 256;
constexpr size_t MessageSize = 0x100;

/// Checks that @p process is FS with the requested process ID, and that the
/// range fits the caller's buffer and the command's limit.
Result checkFsRequest(Handle process, const RoProcessMemoryRequest& request, u64 limit, u64 capacity) {
    u64 process_id = 0;
    u64 program_id = 0;
    Result rc = svcGetProcessId(&process_id, process);
    if (R_SUCCEEDED(rc))
        rc = svcGetInfo(&program_id, InfoType_ProgramId, process, 0);
    if (R_FAILED(rc))
        return rc;

    if (program_id != FilesystemProgramId || request.process_id != process_id || !request.size ||
        request.size > limit || request.size > capacity || request.address > UINT64_MAX - request.size)
        return RoResult_BadRequest;
    return 0;
}

/// Checks that the range lies in one block of FS code memory: static or
/// mutable code for a read, writable mutable code for a write.
Result checkCodeBlock(Handle process, const RoProcessMemoryRequest& request, bool write) {
    MemoryInfo info = {};
    u32 page = 0;
    const Result rc = svcQueryProcessMemory(&info, &page, process, request.address);
    if (R_FAILED(rc))
        return rc;

    const bool inside = request.address >= info.addr && request.size <= info.size &&
                        request.address - info.addr <= info.size - request.size;
    const bool code = write ? (info.type == MemType_CodeMutable && (info.perm & Perm_W))
                            : (info.type == MemType_CodeStatic || info.type == MemType_CodeMutable);
    if (!inside || !code || info.attr)
        return RoResult_BadRequest;
    return 0;
}

/// The pages of a range, mapped into RO.
struct MappedRange {
    void* local;
    u64 page_address;
    u64 mapping_size;
    u8* bytes; ///< The range itself, within the mapped pages.
};

Result mapRange(Handle process, u64 address, u64 size, MappedRange* out) {
    const u64 page_address = address & ~(PageSize - 1);
    const u64 within_page = address - page_address;
    const u64 mapping_size = (within_page + size + PageSize - 1) & ~(PageSize - 1);

    virtmemLock();
    void* local = virtmemFindAslr(mapping_size, PageSize);
    if (!local) {
        virtmemUnlock();
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    const Result rc = svcMapProcessMemory(local, process, page_address, mapping_size);
    virtmemUnlock();
    if (R_FAILED(rc))
        return rc;

    *out = MappedRange{local, page_address, mapping_size, static_cast<u8*>(local) + within_page};
    return 0;
}

Result unmapRange(Handle process, const MappedRange& range) {
    return svcUnmapProcessMemory(range.local, process, range.page_address, range.mapping_size);
}

Result readFsMemory(Handle process, const RoProcessMemoryRequest& request, void* output, u64 capacity) {
    Result rc = checkFsRequest(process, request, MaximumReadSize, capacity);
    if (R_SUCCEEDED(rc))
        rc = checkCodeBlock(process, request, false);
    MappedRange range;
    if (R_SUCCEEDED(rc))
        rc = mapRange(process, request.address, request.size, &range);
    if (R_FAILED(rc))
        return rc;

    std::memcpy(output, range.bytes, request.size);
    return unmapRange(process, range);
}

Result writeFsMemory(Handle process, const RoProcessMemoryRequest& request, const void* input, u64 capacity) {
    Result rc = checkFsRequest(process, request, MaximumWriteSize, capacity);
    if (R_SUCCEEDED(rc))
        rc = checkCodeBlock(process, request, true);
    MappedRange range;
    if (R_SUCCEEDED(rc))
        rc = mapRange(process, request.address, request.size, &range);
    if (R_FAILED(rc))
        return rc;

    std::memcpy(range.bytes, input, request.size);
    armDCacheFlush(range.bytes, request.size);
    return unmapRange(process, range);
}

/// The CMIF header, or null when it does not fit with @p payload_size bytes
/// after it.
const CmifInHeader* getHeader(void* message, const HipcParsedRequest& request, size_t payload_size) {
    const u8* end = reinterpret_cast<const u8*>(request.data.data_words + request.meta.num_data_words);
    const auto* header = static_cast<const CmifInHeader*>(cmifGetAlignedDataStart(request.data.data_words, message));
    if (end > static_cast<const u8*>(message) + MessageSize ||
        reinterpret_cast<const u8*>(header + 1) + payload_size > end)
        return nullptr;
    return header;
}

/// True for a Request without a PID or descriptors other than the given buffers.
bool hasShape(const HipcParsedRequest& request, u32 copy_handles, u32 send_buffers, u32 recv_buffers) {
    const HipcMetadata& meta = request.meta;
    return meta.type == CmifCommandType_Request && !meta.send_pid &&
           meta.num_copy_handles == copy_handles && !meta.num_move_handles &&
           !meta.num_send_statics && meta.num_send_buffers == send_buffers &&
           meta.num_recv_buffers == recv_buffers && !meta.num_exch_buffers && !meta.num_recv_statics;
}

/// Answers RoDiagnosticsCommand, if that is the request.
bool serveDiagnostics(void* message, const HipcParsedRequest& request) {
    const CmifInHeader* header = getHeader(message, request, 0);
    if (!header || !hasShape(request, 0, 0, 1) || header->magic != CMIF_IN_HEADER_MAGIC ||
        header->command_id != RoDiagnosticsCommand)
        return false;

    const RoDiagnostics diagnostics = roGetDiagnostics();
    void* output = hipcGetBufferAddress(&request.data.recv_buffers[0]);
    const u64 capacity = hipcGetBufferSize(&request.data.recv_buffers[0]);
    Result rc = RoResult_BadRequest;
    if (output && capacity >= sizeof(diagnostics)) {
        std::memcpy(output, &diagnostics, sizeof(diagnostics));
        rc = 0;
    }
    roWriteReply(message, rc, 0, false);
    return true;
}

/// Runs a read or write request, or returns RoResult_BadRequest for any other.
Result runMemoryCommand(void* message, const HipcParsedRequest& request) {
    const CmifInHeader* header = getHeader(message, request, sizeof(RoProcessMemoryRequest));
    if (!header || header->magic != CMIF_IN_HEADER_MAGIC)
        return RoResult_BadRequest;

    const bool read = header->command_id == RoProcessReadCommand;
    const bool write = header->command_id == RoProcessWriteCommand;
    if ((!read && !write) || !hasShape(request, 1, write ? 1 : 0, read ? 1 : 0))
        return RoResult_BadRequest;

    const Handle process = request.data.copy_handles[0];
    if (process == INVALID_HANDLE)
        return RoResult_BadRequest;

    RoProcessMemoryRequest args;
    std::memcpy(&args, header + 1, sizeof(args));
    if (read) {
        return readFsMemory(process, args, hipcGetBufferAddress(&request.data.recv_buffers[0]),
                            hipcGetBufferSize(&request.data.recv_buffers[0]));
    }
    return writeFsMemory(process, args, hipcGetBufferAddress(&request.data.send_buffers[0]),
                         hipcGetBufferSize(&request.data.send_buffers[0]));
}

void closeRequestHandles(const HipcParsedRequest& request) {
    for (u32 i = 0; i < request.meta.num_copy_handles; i++) {
        if (request.data.copy_handles[i] != INVALID_HANDLE)
            svcCloseHandle(request.data.copy_handles[i]);
    }
    for (u32 i = 0; i < request.meta.num_move_handles; i++) {
        if (request.data.move_handles[i] != INVALID_HANDLE)
            svcCloseHandle(request.data.move_handles[i]);
    }
}

} // namespace

void roFsMemoryDispatch(void* message) {
    if (roReplyQueryPointerBufferSize(message))
        return;

    const HipcParsedRequest request = hipcParseRequest(message);
    if (serveDiagnostics(message, request))
        return;

    const Result rc = runMemoryCommand(message, request);
    closeRequestHandles(request);
    roWriteReply(message, rc, 0, false);
}
