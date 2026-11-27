// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <switch/types.h>

constexpr u32 RoDiagnosticsCommand = 0x524f0018;

struct RoDiagnostics {
    u32 accepted_ldr_set1;
    u32 accepted_ldr_set2;
    u32 accepted_dmnt;
    u32 ldr_requests;
    u32 parsed_requests;
    u32 rejected_requests;
    u32 last_port;
    u32 last_type;
    u32 last_command;
    Result last_result;
    u32 last_data_words;
    u32 last_copy_handles;
    u32 last_move_handles;
    /// Client contexts whose views RO could not unmap when they closed. RO
    /// releases the process anyway; each such view stays mapped in RO.
    u32 cleanup_failures;
    u8 last_request[64];
    u32 image_rejections;
    Result last_cleanup_result; ///< The last of those failures.
    u64 last_image_size;
    u64 last_bss_size;
    u8 last_image_header[0x80];
};
static_assert(sizeof(RoDiagnostics) == 272);
