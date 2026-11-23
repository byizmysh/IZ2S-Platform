#include "ps2_ee_warm_ranges.h"
#include <sb4/jit-cache.h>
#include <kaihen.h>
#include <switch/arm/cache.h>
#include <switch/arm/thread_context.h>
#include <stdatomic.h>
#include <switch/arm/counter.h>
#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
#include <switch/kernel/virtmem.h>
#include <switch/result.h>
#include <switch/runtime/env.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef IZ_JIT_MODEL_TEST
#include <switch/runtime/devices/fs_dev.h>
#include "ps2_sb4_layout.h"
#endif

#ifndef IZ_JIT_BUILD_ID
#error Build must supply IZ_JIT_BUILD_ID for runtime identification
#endif

// Direct CodeMemory or the documented libkaihen persistent RO alias backend.
// Track individual direct releases: jitClose in libounce d1718bb does not remember
// UnmapOwner success when UnmapSlave fails, and ignores CloseHandle's Result.
typedef struct {
    size_t size;
    void *source;
    Handle handle;
    uint8_t *rw, *rx;
    Thread *thread;
    bool rw_mapped, rx_mapped, qualified;
    bool kaihen_backend;
    bool primary_cache;
    bool scalar_publication_only, r799_publication_control;
    bool checked_pair_publication, checked_warm_publication;
    Jit jit;
} CodeCache;

extern Result iz_kaihen_jit_publish_ranges_r799(Jit*, const KaihenJitRange*, size_t);

static Result bad_input(void) { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
static Result unavailable(void) { return MAKERESULT(Module_Libnx, LibnxError_JitUnavailable); }

static uint64_t session_tick;

#ifndef IZ_JIT_MODEL_TEST
typedef struct {
    uint64_t magic;
    char build[32];
    uint64_t session_tick, vm_tick, image_base, jit_base, jit_size;
    uint64_t pc, lr, sp, far;
    uint32_t description, esr, pstate, reserved;
} IzJitFaultRecord;

_Static_assert(sizeof(IzJitFaultRecord) == 128, "Fault record layout");
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "Exception flag must be lock-free");
extern void iz_fault_image_entry(void) __asm__("_start");

// The SDK default stack is 1 KiB. The recorder uses filesystem IPC after
// returning from the kernel exception context, so provide ample private space.
_Alignas(16) unsigned char __nx_exception_stack[64 * 1024];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
// HEN may run the title with a debugger attached. Preserve this application's
// exception record instead of letting the SDK skip the user handler.
uint32_t __nx_exception_ignoredebug = 1;

static atomic_bool s_fault_armed;
static FsFile s_fault_file;
static bool s_fault_file_open;
static IzJitFaultRecord s_fault_record = {
    .magic = UINT64_C(0x31544c5541465a49), // IZFAULT1
    .build = IZ_JIT_BUILD_ID,
};

static void disarm_fault_record(void)
{
    // The VM stops its providers before normal JIT teardown.
    atomic_store_explicit(&s_fault_armed, false, memory_order_release);
    if (s_fault_file_open) {
        fsFileClose(&s_fault_file);
        s_fault_file_open = false;
    }
}

static void arm_fault_record(const Sb4JitCache *cache, size_t bytes)
{
    if (s_fault_file_open)
        return;
    FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
    FsFile marker;
    if (!fs || R_FAILED(fsFsOpenFile(fs, PS2_SB4_SD_ROOT "/ee-startup-trace.flag",
                                    FsOpenMode_Read, &marker)))
        return;
    fsFileClose(&marker);
    const char *path = PS2_SB4_SD_ROOT "/native-fault-latest.bin";
    if (R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Write, &s_fault_file))) {
        if (R_FAILED(fsFsCreateFile(fs, path, sizeof(s_fault_record), 0)) ||
            R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Write, &s_fault_file)))
            return;
    }
    const uint64_t empty = 0;
    if (R_FAILED(fsFileSetSize(&s_fault_file, sizeof(s_fault_record))) ||
        R_FAILED(fsFileWrite(&s_fault_file, 0, &empty, sizeof(empty), FsWriteOption_Flush))) {
        fsFileClose(&s_fault_file);
        return;
    }
    s_fault_file_open = true;
#ifdef IZPS2_NATIVE_FATAL_RECORD
    FILE *runtime_log = freopen("sdmc:" PS2_SB4_SD_ROOT "/runtime-latest.log", "w", stderr);
    if (runtime_log)
        setvbuf(runtime_log, NULL, _IONBF, 0);
#endif
    s_fault_record.session_tick = session_tick;
    s_fault_record.vm_tick = armGetSystemTick();
    s_fault_record.image_base = (uintptr_t)&iz_fault_image_entry;
    s_fault_record.jit_base = (uintptr_t)cache->rx;
    s_fault_record.jit_size = bytes;
    atomic_store_explicit(&s_fault_armed, true, memory_order_release);
}

#ifdef IZPS2_NATIVE_FATAL_RECORD
bool iz_ounce_native_trace_enabled(void)
{
    return atomic_load_explicit(&s_fault_armed, memory_order_acquire);
}

static void record_fatal(uint32_t kind, uint32_t result, uintptr_t caller)
{
    if (!atomic_exchange_explicit(&s_fault_armed, false, memory_order_acq_rel))
        return;
    s_fault_record.pc = caller >= 4 ? caller - 4 : caller;
    s_fault_record.lr = caller;
    s_fault_record.description = kind;
    s_fault_record.esr = result;
    s_fault_record.reserved = 1;
    fsFileWrite(&s_fault_file, 0, &s_fault_record, sizeof(s_fault_record), FsWriteOption_Flush);
}

extern void __real_abort(void) __attribute__((noreturn));
void __wrap_abort(void)
{
    record_fatal(0xf001, 0, (uintptr_t)__builtin_return_address(0));
    __real_abort();
}

extern void __real_diagAbortWithResult(Result result) __attribute__((noreturn));
void __wrap_diagAbortWithResult(Result result)
{
    record_fatal(0xf002, result, (uintptr_t)__builtin_return_address(0));
    __real_diagAbortWithResult(result);
}

extern Result __real_svcBreak(u32 reason, uintptr_t address, uintptr_t size);
Result __wrap_svcBreak(u32 reason, uintptr_t address, uintptr_t size)
{
    if (!(reason & UINT32_C(0x80000000)))
        record_fatal(0xf003, reason, (uintptr_t)__builtin_return_address(0));
    return __real_svcBreak(reason, address, size);
}
#endif

void __libnx_exception_handler(ThreadExceptionDump *dump)
{
    if (!dump || !atomic_exchange_explicit(&s_fault_armed, false, memory_order_acq_rel))
        return;

    s_fault_record.pc = dump->pc.x;
    s_fault_record.lr = dump->lr.x;
    s_fault_record.sp = dump->sp.x;
    s_fault_record.far = dump->far.x;
    s_fault_record.description = dump->error_desc;
    s_fault_record.esr = dump->esr;
    s_fault_record.pstate = dump->pstate;
    // The dedicated file is already open. Do not allocate, format, lock the
    // emulator logger, dereference the fault address or try to resume.
    fsFileWrite(&s_fault_file, 0, &s_fault_record, sizeof(s_fault_record), FsWriteOption_Flush);
    // The SDK exception entry retains its original termination path.
}
#endif

const char *iz_ounce_jit_build_id(void) { return IZ_JIT_BUILD_ID; }
uint64_t iz_ounce_jit_session_tick(void) { return session_tick; }

static void record(const char *line)
{
#ifndef IZ_JIT_MODEL_TEST
    // Keep evidence even if freopen(stderr) failed during early startup.
    // This uses the mounted SDK filesystem and flushes each diagnostic line.
    static Mutex mutex;
    mutexLock(&mutex);
    FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
    FsFile file;
    Result rc = unavailable();
    if (fs) {
        const char *path = "/iz2s/izlauncher-runtime.log";
        rc = fsFsOpenFile(fs, path, FsOpenMode_Write | FsOpenMode_Append, &file);
        if (R_FAILED(rc)) {
            fsFsCreateFile(fs, path, 0, 0);
            rc = fsFsOpenFile(fs, path, FsOpenMode_Write | FsOpenMode_Append, &file);
        }
        if (R_SUCCEEDED(rc)) {
            s64 size = 0;
            rc = fsFileGetSize(&file, &size);
            if (R_SUCCEEDED(rc)) rc = fsFileWrite(&file, size, line, strlen(line), FsWriteOption_Flush);
            fsFileClose(&file);
        }
    }
    mutexUnlock(&mutex);
    if (R_SUCCEEDED(rc)) return;
#endif
    fputs(line, stderr);
}

static void report(const CodeCache *code, const char *stage, size_t bytes, Result rc)
{
    char line[320];
    snprintf(line, sizeof line, "IZLAUNCHER_SDK_JIT build=%s backend=%s stage=%s bytes=%zu rc=0x%08x\n",
             IZ_JIT_BUILD_ID, code ? (code->kaihen_backend ? "kaihen-ro" : "codememory") : "none", stage, bytes, rc);
    record(line);
}

void iz_ounce_jit_cleanup_report(const char *stage, uint32_t primary, uint32_t cleanup)
{
    char line[320];
    snprintf(line, sizeof line, "IZLAUNCHER_SDK_JIT build=%s backend=kaihen-ro stage=%s primary=0x%08x cleanup=0x%08x\n",
             IZ_JIT_BUILD_ID, stage, primary, cleanup);
    record(line);
}

void iz_ounce_jit_session(void)
{
    char line[320];
    session_tick = armGetSystemTick();
    Service ro;
    const Result ro_result = kaihenGetRoSession(&ro);
    snprintf(line, sizeof line, "IZLAUNCHER_JIT_SESSION build=%s tick=%llu create_hint=%d control_hint=%d kaihen_ro=0x%08x\n",
             IZ_JIT_BUILD_ID, (unsigned long long)session_tick,
             envIsSyscallHinted(0x4b), envIsSyscallHinted(0x4c), ro_result);
    record(line);
}

static uint32_t close_cache(Sb4JitCache *cache)
{
    if (!cache) return bad_input();
    CodeCache *code = cache->owner;
    if (!code) return 0;
    code->qualified = false; // A closing cache cannot be used again.
#ifndef IZ_JIT_MODEL_TEST
    if (code->primary_cache) disarm_fault_record();
#endif
    if (code->kaihen_backend) {
        // Failed SDK opens can retain source, staging and the reservation.
        // Never free pages whose remote mapping could still be owned by RO.
        const Result rc = code->jit.src_addr ? jitClose(&code->jit) : 0;
        if (R_FAILED(rc)) { report(code, "close-kaihen", code->size, rc); return rc; }
        report(code, "closed", code->size, 0);
        free(code);
        *cache = (Sb4JitCache){0};
        return 0;
    }
    Result first = 0, rc;
    if (code->rw_mapped) {
        rc = svcControlCodeMemory(code->handle, CodeMapOperation_UnmapOwner, code->rw, code->size, 0);
        if (R_SUCCEEDED(rc)) code->rw_mapped = false;
        else { first = rc; report(code, "close-unmap-rw", code->size, rc); }
    }
    if (code->rx_mapped) {
        rc = svcControlCodeMemory(code->handle, CodeMapOperation_UnmapSlave, code->rx, code->size, 0);
        if (R_SUCCEEDED(rc)) code->rx_mapped = false;
        else { if (!first) first = rc; report(code, "close-unmap-rx", code->size, rc); }
    }
    if (!code->rw_mapped && !code->rx_mapped && code->handle != INVALID_HANDLE) {
        rc = svcCloseHandle(code->handle);
        if (R_SUCCEEDED(rc)) code->handle = INVALID_HANDLE;
        else { if (!first) first = rc; report(code, "close-handle", code->size, rc); }
    }
    if (!first) {
        report(code, "closed", code->size, 0);
        free(code->source);
        free(code);
        *cache = (Sb4JitCache){0};
    }
    // Failed releases retain owner and all resources not confirmed released.
    return first;
}

static bool can_publish(const CodeCache *code, const Sb4JitCache *cache)
{
    return code && code->qualified && code->rw_mapped && code->rx_mapped &&
        cache->rw == code->rw && cache->rx == code->rx && code->thread == threadGetSelf();
}

const char *iz_ounce_jit_publication_mode(const Sb4JitCache *cache)
{
    const CodeCache *code = cache ? cache->owner : NULL;
    if (!can_publish(code, cache))
        return "unavailable";
    if (!code->kaihen_backend)
        return "codememory";
    if (code->r799_publication_control)
        return "kaihen-r799-single-range";
    return code->scalar_publication_only ? "kaihen-scalar" : "kaihen-batch";
}

static bool valid_range(const CodeCache *code, size_t offset, size_t bytes)
{
    return bytes && offset < code->size && bytes <= code->size - offset;
}

static uint32_t publish_cache(const Sb4JitCache *cache, size_t offset, size_t bytes)
{
    CodeCache *code = cache ? cache->owner : NULL;
    if (!can_publish(code, cache) || !valid_range(code, offset, bytes)) {
        report(code, "publish-invalid", code ? code->size : 0, bad_input());
        return bad_input();
    }
    if (code->kaihen_backend) {
        // The writer thread owns staging until this synchronous copy returns.
        const KaihenJitRange range = {offset, bytes};
        const Result rc = code->r799_publication_control
            ? iz_kaihen_jit_publish_ranges_r799(&code->jit, &range, 1)
            : kaihenPublishJitAlias(&code->jit, offset, cache->rw + offset, bytes);
        if (R_FAILED(rc)) {
            code->qualified = false;
            report(code, "publish-kaihen", bytes, rc);
            return rc;
        }
        if (!code->r799_publication_control)
            armICacheInvalidate(cache->rx + offset, bytes);
        return 0;
    }
    // SDK cache.s: DC CIVAC + DSB SY; IC IVAU + DSB SY + ISB.
    // The client excludes execution during WRITES as well as publication.
    armDCacheFlush(cache->rw + offset, bytes);
    armICacheInvalidate(cache->rx + offset, bytes);
    return 0;
}

static bool qualification_image_executes(const Sb4JitCache *cache, size_t tail,
    unsigned first, unsigned last)
{
    typedef unsigned (*Probe)(void);
    const Probe entry = (Probe)(uintptr_t)cache->rx;
    const Probe next = (Probe)(uintptr_t)(cache->rx + 16);
    const Probe end = (Probe)(uintptr_t)(cache->rx + tail);
    // Check bytes before executing code that a failed publication could corrupt.
    return !memcmp(cache->rw, cache->rx, 28) &&
        !memcmp(cache->rw + tail, cache->rx + tail, 12) &&
        entry() == first && next() == 31 && end() == last;
}

static Result qualify_executable_batch(const Sb4JitCache *cache, size_t tail)
{
    CodeCache *code = cache->owner;
    const uint32_t first_value = 0x528004a0; // MOV W0, #37
    const uint32_t last_value = 0x52800520; // MOV W0, #41
    const KaihenJitRange ranges[] = {
        {sizeof(uint32_t), sizeof(first_value)},
        {tail + sizeof(uint32_t), sizeof(last_value)},
    };
    memcpy(cache->rw + ranges[0].offset, &first_value, sizeof(first_value));
    memcpy(cache->rw + ranges[1].offset, &last_value, sizeof(last_value));
    const size_t changed_bytes = sizeof(first_value) + sizeof(last_value);
    Result rc = kaihenJitPublishRanges(&code->jit, ranges, 2);
    if (R_SUCCEEDED(rc) && qualification_image_executes(cache, tail, 37, 41)) {
        report(code, "qualified-batch-37-41", changed_bytes, 0);
        return 0;
    }

    report(code, "qualify-executable-batch", changed_bytes,
        R_FAILED(rc) ? rc : unavailable());
    code->scalar_publication_only = true;
    // Restore whole probe bodies, including neighbours, before another call.
    rc = publish_cache(cache, 0, 28);
    if (R_SUCCEEDED(rc))
        rc = publish_cache(cache, tail, 12);
    if (R_FAILED(rc))
        return rc;
    if (!qualification_image_executes(cache, tail, 37, 41))
        return unavailable();
    report(code, "qualified-scalar-after-batch", changed_bytes, 0);
    return 0;
}

static uint32_t qualify_cache(const Sb4JitCache *cache)
{
    CodeCache *code = cache->owner;
    const uint32_t initial[] = {0xd503245f, 0x52800220, 0xd65f03c0}; // BTI; 17; RET
    const uint32_t neighbor[] = {0xd503245f, 0x528003e0, 0xd65f03c0}; // BTI; 31; RET
    const uint32_t replacement = 0x528002e0; // MOV W0, #23
    const size_t tail = code->size - sizeof(neighbor);
    unsigned (*entry)(void) = (unsigned (*)(void))(uintptr_t)cache->rx;
    unsigned (*next)(void) = (unsigned (*)(void))(uintptr_t)(cache->rx + 16);
    unsigned (*last)(void) = (unsigned (*)(void))(uintptr_t)(cache->rx + tail);
    memcpy(cache->rw, initial, sizeof(initial));
    memcpy(cache->rw + 16, neighbor, sizeof(neighbor)); // Same cache line.
    memcpy(cache->rw + tail, neighbor, sizeof(neighbor)); // Allocation boundary.
    Result rc = publish_cache(cache, 0, 16 + sizeof(neighbor));
    if (R_SUCCEEDED(rc)) rc = publish_cache(cache, tail, sizeof(neighbor));
    if (R_FAILED(rc)) return rc;
    if (entry() != 17 || next() != 31 || last() != 31) return unavailable();
    memcpy(cache->rw + sizeof(uint32_t), &replacement, sizeof(replacement));
    rc = publish_cache(cache, sizeof(uint32_t), sizeof(replacement));
    if (R_FAILED(rc)) return rc;
    if (entry() != 23 || next() != 31 || last() != 31 ||
        memcmp(cache->rw + 16, neighbor, sizeof(neighbor)) ||
        memcmp(cache->rw + tail, neighbor, sizeof(neighbor)) ||
        memcmp(cache->rx + 16, neighbor, sizeof(neighbor)) ||
        memcmp(cache->rx + tail, neighbor, sizeof(neighbor))) return unavailable();
    report(code, "qualified-17-23-neighbors-31", code->size, 0);
    return code->kaihen_backend && !code->scalar_publication_only
        ? qualify_executable_batch(cache, tail) : 0;
}

static bool use_scalar_publication_control(bool r799)
{
#ifndef IZ_JIT_MODEL_TEST
    const char *path = r799 ? PS2_SB4_SD_ROOT "/jit-r799-publication.flag"
                           : PS2_SB4_SD_ROOT "/jit-scalar-publication.flag";
    FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
    FsFile file;
    if (fs && R_SUCCEEDED(fsFsOpenFile(fs, path, FsOpenMode_Read, &file))) {
        fsFileClose(&file);
        return true;
    }
#else
    (void)r799;
#endif
    return false;
}

static uint32_t open_cache_for_domain(Sb4JitCache *cache, size_t bytes, bool primary)
{
    if (!cache || cache->owner || cache->rw || cache->rx || !bytes ||
        bytes % 4096 || bytes > SIZE_MAX - 4095) {
        report(NULL, "open-invalid", bytes, bad_input());
        return bad_input();
    }
    const bool create = envIsSyscallHinted(0x4b), control = envIsSyscallHinted(0x4c);
    const bool use_kaihen = !create || !control;
    if (use_kaihen) {
        Service ro;
        if (R_FAILED(kaihenGetRoSession(&ro))) {
            report(NULL, "capability-no-ro-service", bytes, unavailable());
            return unavailable();
        }
    }
    CodeCache *code = calloc(1, sizeof(*code));
    if (!code) {
        Result rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        report(NULL, "allocate-owner", bytes, rc);
        return rc;
    }
    code->size = bytes;
    code->primary_cache = primary;
    code->kaihen_backend = use_kaihen;
    code->r799_publication_control = use_kaihen && use_scalar_publication_control(true);
    const bool scalar_control = code->r799_publication_control ||
        (use_kaihen && use_scalar_publication_control(false));
    code->scalar_publication_only = scalar_control;
    code->handle = INVALID_HANDLE;
    code->thread = threadGetSelf();
    cache->owner = code;
    const char *stage = "allocate-source";
    Result rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    if (use_kaihen) {
        stage = "kaihen-jit-create";
        rc = jitCreate(&code->jit, bytes);
        if (R_FAILED(rc)) goto failed;
        stage = "kaihen-persistent-alias";
        if (code->jit.type != JitType_SetProcessMemoryPermission ||
            code->jit.rw_addr == code->jit.src_addr) { rc = unavailable(); goto failed; }
        rc = jitTransitionToWritable(&code->jit);
        if (R_FAILED(rc)) goto failed;
        code->rw = jitGetRwAddr(&code->jit);
        code->rx = jitGetRxAddr(&code->jit);
        code->rw_mapped = code->rw != NULL;
        code->rx_mapped = code->rx != NULL;
        goto qualify;
    }
    code->source = memalign(4096, bytes);
    if (!code->source) goto failed;
    stage = "create";
    // The SDK SVC wrapper stores its output register even when Result fails.
    // Only a successful create transfers ownership of that handle to us.
    Handle created = INVALID_HANDLE;
    rc = svcCreateCodeMemory(&created, code->source, bytes);
    if (R_FAILED(rc)) goto failed;
    code->handle = created;
    stage = "map-rw";
    virtmemLock();
    code->rw = virtmemFindCodeMemory(bytes, 4096);
    rc = code->rw ? svcControlCodeMemory(code->handle, CodeMapOperation_MapOwner,
            code->rw, bytes, Perm_Rw) : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    code->rw_mapped = R_SUCCEEDED(rc);
    virtmemUnlock();
    if (R_FAILED(rc)) goto failed;
    stage = "map-rx";
    virtmemLock();
    code->rx = virtmemFindCodeMemory(bytes, 4096);
    rc = code->rx ? svcControlCodeMemory(code->handle, CodeMapOperation_MapSlave,
            code->rx, bytes, Perm_Rx) : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    code->rx_mapped = R_SUCCEEDED(rc);
    virtmemUnlock();
    if (R_FAILED(rc)) goto failed;
qualify:
    cache->rw = code->rw;
    cache->rx = code->rx;
    stage = "qualify";
#ifndef IZ_JIT_MODEL_TEST
    if (primary) arm_fault_record(cache, bytes);
#endif
    code->qualified = true; // Private to open until qualification returns.
    rc = code->rw != code->rx ? qualify_cache(cache) : unavailable();
    if (R_SUCCEEDED(rc)) {
        report(code, code->r799_publication_control ? "opened-r799-publication-control" :
            scalar_control ? "opened-scalar-control" :
            code->scalar_publication_only ? "opened-scalar-recovery" : "opened", bytes, 0);
        return 0;
    }
failed:
    report(code, stage, bytes, rc);
    Result cleanup = close_cache(cache);
    if (R_FAILED(cleanup)) report(cache->owner, "open-cleanup", bytes, cleanup);
    return rc; // Preserve the primary Result separately from cleanup errors.
}

static uint32_t open_cache(Sb4JitCache *cache, size_t bytes)
{
    return open_cache_for_domain(cache, bytes, true);
}

static uint32_t open_iop_cache(Sb4JitCache *cache, size_t bytes)
{
    return open_cache_for_domain(cache, bytes, false);
}

// The worker opens, publishes, and closes its own lease. This API never
// arms or disarms the primary VM arena's fatal-record file.
const Sb4JitCacheApi *iz_ounce_iop_jit_cache_api(void)
{
    static const Sb4JitCacheApi api = {open_iop_cache, publish_cache, close_cache};
    return &api;
}

const Sb4JitCacheApi *iz_ounce_jit_cache_api(void)
{
    static const Sb4JitCacheApi api = {open_cache, publish_cache, close_cache};
    return &api;
}

// Publication is synchronous. The RX view is readable, as already required
// by qualify_cache(). Check only the first multi-range operation of each kind.
static bool published_ranges_match(const Sb4JitCache *cache,
    const KaihenJitRange *ranges, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const size_t offset = ranges[i].offset;
        if (memcmp(cache->rw + offset, cache->rx + offset, ranges[i].size))
            return false;
    }
    return true;
}

// The VM is parked during publication. Retry every member, even if the
// rejected batch copied a prefix, so dispatch always sees the same bytes.
static Result publish_validated_ranges(const Sb4JitCache *cache, CodeCache *code,
    const KaihenJitRange *ranges, size_t count, size_t bytes, const char *stage, bool *checked)
{
    bool verify_retry = false;
    if (code->kaihen_backend && !code->scalar_publication_only) {
        const Result batch = kaihenJitPublishRanges(&code->jit, ranges, count);
        if (R_SUCCEEDED(batch)) {
            if (*checked || published_ranges_match(cache, ranges, count)) {
                *checked = true;
                return 0;
            }
            report(code, "publish-batch-rx-mismatch", bytes, unavailable());
            verify_retry = true;
        } else {
            report(code, stage, bytes, batch);
        }
        // A rejected batch leaves the validated owner and mappings intact.
        // The scalar path disqualifies the cache if the retry also fails.
        code->scalar_publication_only = true;
    }

    for (size_t i = 0; i < count; ++i) {
        const Result retry = publish_cache(cache, ranges[i].offset, ranges[i].size);
        if (R_FAILED(retry))
            return retry;
    }
    if (verify_retry && !published_ranges_match(cache, ranges, count)) {
        code->qualified = false;
        const Result rc = unavailable();
        report(code, "publish-scalar-rx-mismatch", bytes, rc);
        return rc;
    }
    return 0;
}

uint32_t iz_ounce_publish_jit_pair(const Sb4JitCache *cache,
    size_t first_offset, size_t first_size, size_t second_offset, size_t second_size)
{
    CodeCache *code = cache ? cache->owner : NULL;
    if (!can_publish(code, cache) || !valid_range(code, first_offset, first_size) ||
        !valid_range(code, second_offset, second_size)) {
        report(code, "publish-pair-invalid", code ? code->size : 0, bad_input());
        return bad_input();
    }

    // After each range was checked, adjacency implies the merged size fits.
    if (first_offset + first_size == second_offset)
        return publish_cache(cache, first_offset, first_size + second_size);

    const KaihenJitRange ranges[] = {{first_offset, first_size}, {second_offset, second_size}};
    return publish_validated_ranges(cache, code, ranges, 2, first_size, "publish-pair-kaihen",
        &code->checked_pair_publication);
}

uint32_t iz_ounce_publish_ee_warm_ranges(const Sb4JitCache *cache,
    const Ps2EeWarmRange *ranges, size_t count)
{
    CodeCache *code = cache ? cache->owner : NULL;
    if (!can_publish(code, cache) || !ranges || !count || count > PS2_EE_WARM_MAX_RANGES)
        return bad_input();

    size_t total = 0;
    KaihenJitRange spans[PS2_EE_WARM_MAX_RANGES];
    for (size_t i = 0; i < count; ++i) {
        if (!valid_range(code, ranges[i].offset, ranges[i].size) ||
            ranges[i].size > PS2_EE_WARM_MAX_BYTES - total)
            return bad_input();
        total += ranges[i].size;
        spans[i] = (KaihenJitRange){ranges[i].offset, ranges[i].size};
    }

    if (count == 1)
        return publish_cache(cache, spans[0].offset, spans[0].size);
    return publish_validated_ranges(cache, code, spans, count, total, "publish-ee-warm", &code->checked_warm_publication);
}
