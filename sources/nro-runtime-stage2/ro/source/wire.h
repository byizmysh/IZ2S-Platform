// SPDX-License-Identifier: GPL-2.0-only
// RO's ldr:ro server side: request parsing and replies, on libounce-nx's
// HIPC and CMIF types.
#pragma once

#include <switch/types.h>

#include "../../libkaihen/include/kaihen/protocol.h"

/// ldr:ro command IDs.
enum RoCmd : u32 {
    RoCmd_LoadNro = 0,
    RoCmd_UnloadNro = 1,
    RoCmd_LoadNrr = 2,
    RoCmd_UnloadNrr = 3,
    RoCmd_Initialize = 4,
    /// Private: the bootstrap copies RO a HEN control session to keep.
    RoCmd_ProvisionHenControl = 65001,
    // The homebrew commands (libkaihen/include/kaihen/protocol.h).
    RoCmd_GetHomebrewSession = KaihenRoCommand_GetHomebrewSession,
    RoCmd_MapJitCode = KaihenRoCommand_MapJitCode,
    RoCmd_UnmapJitCode = KaihenRoCommand_UnmapJitCode,
    RoCmd_ReleaseJitCode = KaihenRoCommand_ReleaseJitCode,
    RoCmd_MapJitAlias = KaihenRoCommand_MapJitAlias,
    RoCmd_PublishJitAlias = KaihenRoCommand_PublishJitAlias,
    RoCmd_PublishJitRanges = KaihenRoCommand_PublishJitRanges,
    RoCmd_ReleaseNros = KaihenRoCommand_ReleaseNros,
};

/// A decoded ldr:ro request.
struct RoRequest {
    u32 cmd_id;
    u32 token;          ///< CMIF context token.
    u64 pid;            ///< Sender PID, inserted by the kernel.
    Handle process;     ///< Copied handle for Initialize or ProvisionHenControl, else 0.
    u64 args[4];        ///< Arguments after the PID placeholder.
    const void* input;  ///< Send buffer of PublishJitAlias and PublishJitRanges, else null.
    u64 input_size;
    const void* ranges; ///< Range buffer of PublishJitRanges, else null.
    u64 ranges_size;
};

/**
 * @brief Decodes an ldr:ro request: a CMIF Request or RequestWithContext
 *        with version <= 1, a PID, and the command's handle count, buffer
 *        count and payload size. libnx's ldrRo* (type 4, version 0) and the
 *        SDK's (type 6, version 1) both decode.
 * @param[in] base IPC message buffer (0x100 bytes).
 * @return Whether the request was accepted.
 */
bool roParseRequest(const void* base, RoRequest* out);

/**
 * @brief Answers CMIF Control QueryPointerBufferSize (command 3), which
 *        libnx's serviceCreate sends on each new session, with size 0.
 * @return Whether the message was that request.
 */
bool roReplyQueryPointerBufferSize(void* base);

/// Writes a reply. With has_value and a successful rc, a u64 value follows the CMIF header.
void roWriteReply(void* base, Result rc, u64 value, bool has_value);

/// Writes a reply with a COPY of @p handle on success.
void roWriteHandleReply(void* base, Result rc, Handle handle);

/// Writes the MapJitAlias reply: RO's view of the mapping on success.
void roWriteJitAliasReply(void* base, Result rc, const KaihenJitAliasInfo* info);
