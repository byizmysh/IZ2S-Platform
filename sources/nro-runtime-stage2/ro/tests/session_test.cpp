// SPDX-License-Identifier: GPL-2.0-only
// ldr:ro contexts and the homebrew services session against mocked kernel
// calls and providers. Replies are checked word by word.
#include "../source/session.h"
#include "switch/result.h"
extern "C" {
#include "switch/kernel/svc.h"
}
#include "switch/sf/cmif.h"
#include "switch/sf/hipc.h"
#include <assert.h>
#include <string.h>

static Handle g_closed[8];
static u32 g_closeCount;
static u32 g_mapCount, g_unmapCount;
// roUnmapImage fails with this result for the image at this address.
static Result g_unmapResult;
static u64 g_unmapFailureBase;
static Result g_mapResult;
static u32 g_jitMapCount, g_jitUnmapCount;
static u32 g_aliasMapCount, g_aliasUnmapCount, g_aliasFlushCount;
static Result g_aliasUnmapResult;
alignas(4096) static u8 g_aliasMemory[0x4000];
static Handle g_controlHandle;
Result roControlAdopt(Handle handle, u64* pid) {
    g_controlHandle = handle;
    *pid = 321;
    return 0;
}
Result roControlCreateHomebrewSession(Handle process, Handle* out) {
    assert(g_controlHandle == 55);
    assert(process == 42);
    *out = 77;
    return 0;
}
Result svcCloseHandle(Handle handle) {
    g_closed[g_closeCount++] = handle;
    return 0;
}

Result svcGetProcessId(u64* pid, Handle handle) {
    if (handle != 42 && handle != 43)
        return 0xd401;
    *pid = 213;
    return 0;
}

Result svcMapProcessCodeMemory(Handle process, u64 destination, u64 source, u64 size) {
    assert(process == 42 && destination >= 0x56789000 &&
           destination < 0x56789000 + RoMaxJit * 0x2000ULL &&
           (destination - 0x56789000) % 0x2000 == 0 &&
           source == destination - 0x44444000 && size == 0x2000);
    g_jitMapCount++;
    return 0;
}

Result svcSetProcessMemoryPermission(Handle process, u64 destination, u64 size, u32 permission) {
    assert(process == 42 && destination >= 0x56789000 &&
           destination < 0x56789000 + RoMaxJit * 0x2000ULL &&
           (destination - 0x56789000) % 0x2000 == 0 &&
           size == 0x2000 && permission == Perm_Rx);
    return 0;
}

Result svcUnmapProcessCodeMemory(Handle process, u64 destination, u64 source, u64 size) {
    assert(process == 42 && destination >= 0x56789000 &&
           destination < 0x56789000 + RoMaxJit * 0x2000ULL &&
           (destination - 0x56789000) % 0x2000 == 0 &&
           source == destination - 0x44444000 && size == 0x2000);
    g_jitUnmapCount++;
    return 0;
}

extern "C" void virtmemLock(void) {}
extern "C" void virtmemUnlock(void) {}
extern "C" void* virtmemFindAslr(size_t size, size_t guard_size) {
    assert(size <= sizeof(g_aliasMemory) && guard_size == 0x1000);
    return g_aliasMemory;
}
Result svcMapProcessMemory(void* destination, Handle process, u64 source, u64 size) {
    assert(destination == g_aliasMemory && process == 42 &&
           source == 0x56789000 && size == 0x2000);
    g_aliasMapCount++;
    return 0;
}
Result svcUnmapProcessMemory(void* destination, Handle process, u64 source, u64 size) {
    assert(destination == g_aliasMemory && process == 42 &&
           source == 0x56789000 && size == 0x2000);
    g_aliasUnmapCount++;
    return g_aliasUnmapResult;
}
Result svcQueryMemory(MemoryInfo* info, u32* page, u64 address) {
    assert(address == (u64)g_aliasMemory);
    memset(info, 0, sizeof(*info));
    info->addr = address;
    info->size = sizeof(g_aliasMemory);
    info->perm = Perm_Rw;
    *page = 0;
    return 0;
}
extern "C" void armDCacheFlush(void* address, size_t size) {
    assert((address == g_aliasMemory || address == g_aliasMemory + 8) &&
           size == sizeof(u32));
    g_aliasFlushCount++;
}

// Sends one of the JIT commands that take a destination, source and size.
static void jitRequest(u32* words, u32 command, u64 destination, u64 source, u64 size);

Result roMapImage(Handle process, const RoImageRequest& request, RoLoadedImage* record,
                  const RoLoadedImage* existing, u32 existing_count, u64* base) {
    assert(process == 42 && request.source == 0x12345000 && request.size == 0x3000);
    assert(record >= existing && record < existing + existing_count);
    g_mapCount++;
    if (g_mapResult)
        return g_mapResult;
    // Each record gets an address of its own.
    *base = 0x45678000 + static_cast<u64>(record - existing) * 0x10000;
    record->base = *base;
    record->mapped = RoImageState_Ready | RoImageState_CodeMapped;
    return 0;
}

Result roUnmapImage(Handle process, RoLoadedImage* record) {
    assert(process == 42 && record->base >= 0x45678000 && (record->base & 0xffff) == 0x8000);
    g_unmapCount++;
    if (g_unmapResult && record->base == g_unmapFailureBase)
        return g_unmapResult;
    record->mapped = 0;
    record->base = 0;
    return 0;
}

// An ldr:ro request as the SDK sends it: RequestWithContext, PID, CMIF at word 8.
static void request(u32 words[64], u32 cmd_id, u32 num_data_words, u32 special, Handle handle) {
    memset(words, 0, 64 * sizeof(*words));
    words[0] = 6;
    words[1] = 0x80000000 | num_data_words;
    words[2] = special;
    words[3] = 213;
    words[5] = handle;
    words[8] = 0x49434653;
    words[9] = 1;
    words[10] = cmd_id;
}

int main(void) {
    RoSession sessions[2] = {0};
    u32 words[64];

    request(words, RoCmd_Initialize, 10, 3, 42);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && sessions[0].process_handle == 42);
    assert(sessions[0].process_id == 213 && g_closeCount == 0);

    // A second Initialize for the same process is rejected; its handle is closed.
    request(words, RoCmd_Initialize, 10, 3, 43);
    roDispatch(sessions, 2, 1, words);
    assert(words[6] == RoResult_BadRequest && g_closeCount == 1);
    assert(g_closed[0] == 43 && sessions[1].process_handle == 0);

    request(words, RoCmd_LoadNrr, 14, 1, 0);
    words[14] = 0x22345000;
    words[16] = 0x1000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && sessions[0].nrr_source[0] == 0x22345000);

    request(words, RoCmd_LoadNro, 18, 1, 0);
    words[14] = 0x12345000;
    words[16] = 0x3000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[1] == 10 && words[8] == 0x45678000);
    assert(g_mapCount == 1 && sessions[0].nro[0].base == 0x45678000);

    request(words, RoCmd_UnloadNro, 12, 1, 0);
    words[14] = 0x45678000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && g_unmapCount == 1 && sessions[0].nro[0].mapped == 0);

    request(words, RoCmd_LoadNro, 18, 1, 0);
    words[14] = 0x12345000;
    words[16] = 0x3000;
    g_mapResult = RoResult_InvalidNro;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == RoResult_InvalidNro && words[1] == 8 && words[8] == 0);
    assert(g_mapCount == 2 && sessions[0].nro[0].mapped == 0);
    g_mapResult = 0;

    // The loader releases a program and its libraries in one request. An
    // image that cannot be unmapped stays recorded, and the rest are removed.
    for (u32 i = 0; i < 3; i++) {
        request(words, RoCmd_LoadNro, 18, 1, 0);
        words[14] = 0x12345000;
        words[16] = 0x3000;
        roDispatch(sessions, 2, 0, words);
        assert(words[6] == 0 && words[8] == 0x45678000 + i * 0x10000ULL);
    }
    g_unmapResult = RoResult_InvalidAddress;
    g_unmapFailureBase = 0x45688000;
    const u32 unmaps_before_release = g_unmapCount;
    request(words, RoCmd_ReleaseNros, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == RoResult_InvalidAddress && g_unmapCount == unmaps_before_release + 3);
    assert(sessions[0].nro[0].mapped == 0 && sessions[0].nro[1].mapped != 0 &&
           sessions[0].nro[2].mapped == 0);
    g_unmapResult = 0;
    request(words, RoCmd_ReleaseNros, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == 1 && sessions[0].nro[1].mapped == 0);
    request(words, RoCmd_ReleaseNros, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == 0 && g_unmapCount == unmaps_before_release + 4);

    // The number of NROs has no fixed limit: the records grow, each image
    // gets a record of its own, and a freed record is used again.
    constexpr u32 ManyNros = 300;
    for (u32 i = 0; i < ManyNros; i++) {
        request(words, RoCmd_LoadNro, 18, 1, 0);
        words[14] = 0x12345000;
        words[16] = 0x3000;
        roDispatch(sessions, 2, 0, words);
        assert(words[6] == 0 && words[8] == 0x45678000 + i * 0x10000ULL);
    }
    assert(sessions[0].nro_capacity >= ManyNros);
    request(words, RoCmd_UnloadNro, 12, 1, 0);
    words[14] = 0x45678000 + 5 * 0x10000ULL;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && sessions[0].nro[5].mapped == 0);
    request(words, RoCmd_LoadNro, 18, 1, 0);
    words[14] = 0x12345000;
    words[16] = 0x3000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == 0x45678000 + 5 * 0x10000ULL);
    request(words, RoCmd_ReleaseNros, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == ManyNros);

    // An unaligned range is rejected before any system call.
    jitRequest(words, RoCmd_MapJitCode, 0x56789000, 0x12345800, 0x2000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == RoResult_BadRequest && g_jitMapCount == 0);

    jitRequest(words, RoCmd_MapJitCode, 0x56789000, 0x12345000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && g_jitMapCount == 1 && sessions[0].jit[0].size == 0x2000);

    request(words, RoCmd_MapJitAlias, 14, 1, 0);
    words[14] = 0x56789000;
    words[16] = 0x2000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == (u32)(u64)g_aliasMemory);
    assert(words[10] == 0 && words[11] == Perm_Rw && words[12] == 0);
    assert(g_aliasMapCount == 1 && sessions[0].jit[0].alias == g_aliasMemory);

    const u32 replacement = 0x52800560;
    memset(words, 0, sizeof(words));
    HipcRequest publish = hipcMakeRequestInline(words,
        .type = CmifCommandType_RequestWithContext,
        .num_send_buffers = 1,
        .num_data_words = 16,
        .send_pid = true);
    words[3] = 213;
    publish.send_buffers[0] = hipcMakeBuffer(&replacement, sizeof(replacement),
                                             HipcBufferMode_Normal);
    CmifInHeader* publish_header = (CmifInHeader*)cmifGetAlignedDataStart(publish.data_words, words);
    publish_header->magic = CMIF_IN_HEADER_MAGIC;
    publish_header->version = 1;
    publish_header->command_id = RoCmd_PublishJitAlias;
    u64* publish_args = (u64*)(publish_header + 1);
    publish_args[1] = 0x56789000;
    publish_args[2] = 0;
    publish_args[3] = sizeof(replacement);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && *(u32*)g_aliasMemory == replacement && g_aliasFlushCount == 1);

    KaihenJitRange ranges[2] = {{0, 4}, {8, 4}};
    u32 batch_bytes[2] = {0x52800540, 0xd65f03c0};
    memset(words, 0, sizeof(words));
    HipcRequest batch = hipcMakeRequestInline(words,
        .type = CmifCommandType_RequestWithContext,
        .num_send_buffers = 2,
        .num_data_words = 14,
        .send_pid = true);
    words[3] = 213;
    batch.send_buffers[0] = hipcMakeBuffer(ranges, sizeof(ranges), HipcBufferMode_Normal);
    batch.send_buffers[1] = hipcMakeBuffer(batch_bytes, sizeof(batch_bytes),
                                          HipcBufferMode_Normal);
    CmifInHeader* batch_header = (CmifInHeader*)cmifGetAlignedDataStart(batch.data_words, words);
    batch_header->magic = CMIF_IN_HEADER_MAGIC;
    batch_header->version = 1;
    batch_header->command_id = RoCmd_PublishJitRanges;
    u64* batch_args = (u64*)(batch_header + 1);
    batch_args[1] = 0x56789000;
    batch_args[2] = 2;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && *(u32*)g_aliasMemory == batch_bytes[0] &&
           *(u32*)(g_aliasMemory + 8) == batch_bytes[1] && g_aliasFlushCount == 3);

    ranges[1].offset = 0x3000;
    batch_bytes[0] = 0x11111111;
    memset(words, 0, sizeof(words));
    batch = hipcMakeRequestInline(words,
        .type = CmifCommandType_RequestWithContext,
        .num_send_buffers = 2,
        .num_data_words = 14,
        .send_pid = true);
    words[3] = 213;
    batch.send_buffers[0] = hipcMakeBuffer(ranges, sizeof(ranges), HipcBufferMode_Normal);
    batch.send_buffers[1] = hipcMakeBuffer(batch_bytes, sizeof(batch_bytes),
                                          HipcBufferMode_Normal);
    batch_header = (CmifInHeader*)cmifGetAlignedDataStart(batch.data_words, words);
    batch_header->magic = CMIF_IN_HEADER_MAGIC;
    batch_header->version = 1;
    batch_header->command_id = RoCmd_PublishJitRanges;
    batch_args = (u64*)(batch_header + 1);
    batch_args[1] = 0x56789000;
    batch_args[2] = 2;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == RoResult_BadRequest && *(u32*)g_aliasMemory == 0x52800540 &&
           g_aliasFlushCount == 3);

    // Only a recorded mapping can be unmapped.
    jitRequest(words, RoCmd_UnmapJitCode, 0x56789000, 0x12345000, 0x1000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == RoResult_BadRequest && g_jitUnmapCount == 0);

    jitRequest(words, RoCmd_UnmapJitCode, 0x56789000, 0x12345000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && g_jitUnmapCount == 1 && g_aliasUnmapCount == 1 &&
           sessions[0].jit[0].size == 0);

    // The loader releases what an NRO left mapped.
    jitRequest(words, RoCmd_MapJitCode, 0x56789000, 0x12345000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    jitRequest(words, RoCmd_MapJitCode, 0x5678b000, 0x12347000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && g_jitMapCount == 3);
    request(words, RoCmd_MapJitAlias, 14, 1, 0);
    words[14] = 0x56789000;
    words[16] = 0x2000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && g_aliasMapCount == 2);
    request(words, RoCmd_ReleaseJitCode, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == 2 && g_jitUnmapCount == 3 &&
           g_aliasUnmapCount == 2);
    assert(sessions[0].jit[0].size == 0 && sessions[0].jit[1].size == 0);
    request(words, RoCmd_ReleaseJitCode, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == 0 && g_jitUnmapCount == 3);

    // All 512 client JIT slots can be used, the next map fails without
    // touching the kernel, and ReleaseJitCode makes every slot reusable.
    const u32 maps_before_capacity_test = g_jitMapCount;
    for (u32 i = 0; i < RoMaxJit; i++) {
        const u64 destination = 0x56789000ULL + i * 0x2000ULL;
        jitRequest(words, RoCmd_MapJitCode, destination,
                   destination - 0x44444000ULL, 0x2000);
        roDispatch(sessions, 2, 0, words);
        assert(words[6] == 0 && sessions[0].jit[i].destination == destination);
    }
    assert(g_jitMapCount == maps_before_capacity_test + RoMaxJit);
    jitRequest(words, RoCmd_MapJitCode, 0x60000000, 0x1bbbc000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == RoResult_OutOfAddressSpace &&
           g_jitMapCount == maps_before_capacity_test + RoMaxJit);

    request(words, RoCmd_ReleaseJitCode, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == RoMaxJit);
    for (u32 i = 0; i < RoMaxJit; i++)
        assert(sessions[0].jit[i].size == 0);
    jitRequest(words, RoCmd_MapJitCode, 0x56789000, 0x12345000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && sessions[0].jit[0].size == 0x2000);
    request(words, RoCmd_ReleaseJitCode, 10, 1, 0);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == 1);

    request(words, RoCmd_UnloadNrr, 12, 1, 0);
    words[14] = 0x22345000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && sessions[0].nrr_size[0] == 0);

    // Closing a client removes a retained RO alias before dropping its
    // process handle. Like stock RO, it leaves executable game code mapped.
    jitRequest(words, RoCmd_MapJitCode, 0x56789000, 0x12345000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    request(words, RoCmd_MapJitAlias, 14, 1, 0);
    words[14] = 0x56789000;
    words[16] = 0x2000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && g_aliasMapCount == 3);

    assert(R_SUCCEEDED(roSessionClose(&sessions[0])));
    assert(g_aliasUnmapCount == 3 &&
           g_jitUnmapCount == 3 + RoMaxJit + 1);
    assert(g_closeCount == 2 && g_closed[1] == 42);
    assert(sessions[0].process_handle == 0);
    request(words, RoCmd_ProvisionHenControl, 10, 3, 55);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && words[8] == 321 && g_controlHandle == 55);
    assert(g_closeCount == 2);
    request(words, RoCmd_Initialize, 10, 3, 42);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0);
    request(words, RoCmd_GetHomebrewSession, 10, 1, 0);
    Handle copied = roDispatch(sessions, 2, 0, words);
    assert(copied == 77 && words[2] == 2 && words[3] == 77);
    assert(g_closeCount == 2);
    svcCloseHandle(copied); // The server closes only its reply-side reference.
    assert(g_closeCount == 3 && g_closed[2] == 77);
    // The same borrowed ldr:ro session can be initialized again by an NRO.
    request(words, RoCmd_Initialize, 10, 3, 43);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && sessions[0].process_handle == 42);
    assert(g_closeCount == 4 && g_closed[3] == 43);

    // A view that cannot be unmapped does not keep the process alive: the
    // process handle is still closed, the context is cleared for the next
    // client, and the failure is counted.
    jitRequest(words, RoCmd_MapJitCode, 0x56789000, 0x12345000, 0x2000);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0);
    request(words, RoCmd_MapJitAlias, 14, 1, 0);
    words[14] = 0x56789000;
    words[16] = 0x2000;
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && g_aliasMapCount == 4);
    g_aliasUnmapResult = KERNELRESULT(InvalidState);
    assert(roGetDiagnostics().cleanup_failures == 0);
    assert(roSessionClose(&sessions[0]) == g_aliasUnmapResult);
    assert(g_closeCount == 5 && g_closed[4] == 42);
    assert(sessions[0].process_handle == 0 && sessions[0].process_id == 0);
    assert(sessions[0].jit[0].size == 0 && sessions[0].jit[0].alias == nullptr);
    const RoDiagnostics diagnostics = roGetDiagnostics();
    assert(diagnostics.cleanup_failures == 1 &&
           diagnostics.last_cleanup_result == g_aliasUnmapResult);

    // The cleared context serves the next client.
    request(words, RoCmd_Initialize, 10, 3, 42);
    roDispatch(sessions, 2, 0, words);
    assert(words[6] == 0 && sessions[0].process_handle == 42);
    return 0;
}

static void jitRequest(u32* words, u32 command, u64 destination, u64 source, u64 size) {
    request(words, command, 16, 1, 0);
    words[14] = static_cast<u32>(destination);
    words[16] = static_cast<u32>(source);
    words[18] = static_cast<u32>(size);
}
