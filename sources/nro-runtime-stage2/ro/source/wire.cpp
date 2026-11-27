// SPDX-License-Identifier: GPL-2.0-only
//
// Acceptance follows stock RO (it checks the PID, the handle and buffer
// counts and a sufficient raw extent) and Atmosphere's
// CMIF server (behaviour only, no code: Request or RequestWithContext, SFCI
// with version <= 1, raw data at least the header plus the payload).
#include "wire.h"

#include <cstring>
#include "switch/sf/hipc.h"
#include "switch/sf/cmif.h"

namespace {

constexpr size_t MessageSize = 0x100;
constexpr u32 RoInterfaceToken = 0xa52c55a9;
constexpr u32 CmifControl_QueryPointerBufferSize = 3;

/// What a command carries besides the CMIF header.
struct CommandShape {
    u32 payload_size; ///< Bytes after the CMIF header, the PID placeholder included.
    u32 num_copy_handles;
    u32 num_send_buffers;
};

bool getCommandShape(u32 cmd_id, CommandShape* out) {
    switch (cmd_id) {
    case RoCmd_Initialize:
    case RoCmd_ProvisionHenControl:
        *out = {8, 1, 0};
        return true;
    case RoCmd_GetHomebrewSession:
    case RoCmd_ReleaseJitCode:
    case RoCmd_ReleaseNros:
        *out = {8, 0, 0};
        return true;
    case RoCmd_LoadNro:
        *out = {40, 0, 0};
        return true;
    case RoCmd_MapJitCode:
    case RoCmd_UnmapJitCode:
        *out = {32, 0, 0};
        return true;
    case RoCmd_PublishJitAlias:
        *out = {32, 0, 1};
        return true;
    case RoCmd_MapJitAlias:
    case RoCmd_LoadNrr:
        *out = {24, 0, 0};
        return true;
    case RoCmd_PublishJitRanges:
        *out = {24, 0, 2};
        return true;
    case RoCmd_UnloadNro:
    case RoCmd_UnloadNrr:
        *out = {16, 0, 0};
        return true;
    default:
        return false;
    }
}

bool isPlainRequest(const HipcParsedRequest& r) {
    return (r.meta.type == CmifCommandType_Request || r.meta.type == CmifCommandType_RequestWithContext) &&
           !r.meta.num_send_statics && !r.meta.num_recv_buffers && !r.meta.num_exch_buffers &&
           !r.meta.num_recv_statics;
}

/// The CMIF header, or null when the raw data leaves the message buffer,
/// cannot hold the header, or is not a CMIF request. @p out_end receives the
/// end of the raw data.
const CmifInHeader* getInHeader(const void* base, const HipcParsedRequest& r, const u8** out_end) {
    const u8* end = reinterpret_cast<const u8*>(r.data.data_words + r.meta.num_data_words);
    const auto* header = static_cast<const CmifInHeader*>(
        cmifGetAlignedDataStart(r.data.data_words, const_cast<void*>(base)));

    if (end > static_cast<const u8*>(base) + MessageSize || reinterpret_cast<const u8*>(header + 1) > end)
        return nullptr;
    if (header->magic != CMIF_IN_HEADER_MAGIC || header->version > 1)
        return nullptr;

    *out_end = end;
    return header;
}

const void* sendBufferAddress(const HipcParsedRequest& r, u32 index) {
    return hipcGetBufferAddress(&r.data.send_buffers[index]);
}

u64 sendBufferSize(const HipcParsedRequest& r, u32 index) {
    return hipcGetBufferSize(&r.data.send_buffers[index]);
}

void writeReplyMessage(void* base, Result rc, const void* data, u32 data_size, u32 num_copy_handles,
                       Handle copy_handle) {
    std::memset(base, 0, MessageSize);

    // libnx's data size: the header, the payload and 0x10 bytes of alignment space.
    HipcRequest hipc = hipcMakeRequestInline(base,
        .num_data_words = static_cast<u32>((sizeof(CmifOutHeader) + data_size + 0x10 + 3) / sizeof(u32)),
        .num_copy_handles = num_copy_handles,
    );
    if (num_copy_handles)
        hipc.copy_handles[0] = copy_handle;

    auto* header = static_cast<CmifOutHeader*>(cmifGetAlignedDataStart(hipc.data_words, base));
    header->magic = CMIF_OUT_HEADER_MAGIC;
    header->version = 0;
    header->result = rc;
    header->token = RoInterfaceToken;
    if (data)
        std::memcpy(header + 1, data, data_size);
}

} // namespace

bool roParseRequest(const void* base, RoRequest* out) {
    const HipcParsedRequest r = hipcParseRequest(const_cast<void*>(base));
    if (!isPlainRequest(r) || !r.meta.send_pid || r.meta.num_move_handles)
        return false;

    const u8* end = nullptr;
    const CmifInHeader* header = getInHeader(base, r, &end);
    if (!header)
        return false;

    CommandShape shape;
    if (!getCommandShape(header->command_id, &shape) ||
        r.meta.num_copy_handles != shape.num_copy_handles ||
        r.meta.num_send_buffers != shape.num_send_buffers ||
        reinterpret_cast<const u8*>(header + 1) + shape.payload_size > end ||
        !r.pid || (shape.num_copy_handles && !r.data.copy_handles[0]))
        return false;

    RoRequest request = {};
    request.cmd_id = header->command_id;
    request.token = header->token;
    request.pid = r.pid;
    request.process = shape.num_copy_handles ? r.data.copy_handles[0] : 0;

    // PublishJitAlias sends its bytes; PublishJitRanges sends the ranges, then the bytes.
    if (header->command_id == RoCmd_PublishJitAlias) {
        request.input = sendBufferAddress(r, 0);
        request.input_size = sendBufferSize(r, 0);
    } else if (header->command_id == RoCmd_PublishJitRanges) {
        request.ranges = sendBufferAddress(r, 0);
        request.ranges_size = sendBufferSize(r, 0);
        request.input = sendBufferAddress(r, 1);
        request.input_size = sendBufferSize(r, 1);
    }

    // The payload starts with the PID placeholder; the arguments follow.
    const u8* args = reinterpret_cast<const u8*>(header + 1) + sizeof(u64);
    const u32 num_args = shape.payload_size / sizeof(u64) - 1;
    for (u32 i = 0; i < num_args && i < 4; i++)
        std::memcpy(&request.args[i], args + i * sizeof(u64), sizeof(u64));

    *out = request;
    return true;
}

bool roReplyQueryPointerBufferSize(void* base) {
    const HipcParsedRequest r = hipcParseRequest(base);
    if (r.meta.type != CmifCommandType_Control || r.meta.send_pid || r.meta.num_copy_handles ||
        r.meta.num_move_handles)
        return false;

    const u8* end = nullptr;
    const CmifInHeader* header = getInHeader(base, r, &end);
    if (!header || header->command_id != CmifControl_QueryPointerBufferSize)
        return false;

    roWriteReply(base, 0, 0, false);
    return true;
}

void roWriteReply(void* base, Result rc, u64 value, bool has_value) {
    const bool send_value = has_value && R_SUCCEEDED(rc);
    writeReplyMessage(base, rc, send_value ? &value : nullptr, send_value ? sizeof(value) : 0, 0, INVALID_HANDLE);
}

void roWriteHandleReply(void* base, Result rc, Handle handle) {
    writeReplyMessage(base, rc, nullptr, 0, R_SUCCEEDED(rc) ? 1 : 0, handle);
}

void roWriteJitAliasReply(void* base, Result rc, const KaihenJitAliasInfo* info) {
    const bool send_info = R_SUCCEEDED(rc);
    writeReplyMessage(base, rc, send_info ? info : nullptr, send_info ? sizeof(*info) : 0, 0, INVALID_HANDLE);
}
