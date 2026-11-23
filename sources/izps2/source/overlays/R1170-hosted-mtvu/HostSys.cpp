#include "ps2_ee_warm_ranges.h"
#include "ps2_jit_worker_scratch.h"
#include "ps2_mtvu_jit.h"
#include "ps2_mtvu_jit_handoff.h"
#include "Memory.h"
#include <mutex>
#include <atomic>
#include "common/HostSys.h"
#include "common/Error.h"
#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Switch2/CodeCache.h"
#include "native-system.h"
#include <sb4/jit-cache.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <thread>





namespace
{
// The VM owner allocates the primary arena. The admitted MTVU worker may
// write to its own regions while EE, IOP and VU0 keep their existing owner.
const Sb4JitCacheApi* s_jit_api;
const Sb4JitCacheApi* s_cache_owner;
Sb4JitCache s_cache;
size_t s_cache_size;
constexpr size_t WorkerScratchBytes = 4096;
bool s_worker_scratch_requested;
bool s_worker_scratch_retained;

std::thread::id s_cache_thread;
std::mutex s_sdk_jit_mutex;
MtvuJitHandoff s_mtvu_handoff;
std::atomic_bool s_mtvu_handoff_pending;
bool s_track_mtvu_jit;
constinit thread_local bool s_on_mtvu_worker = false;

constexpr MtvuJitRange VuCode{HostMemoryMap::mVU1recOffset,
    HostMemoryMap::mVU1recOffset + HostMemoryMap::mVU1recSize};
constexpr MtvuJitRange VifCode{HostMemoryMap::VIF1recOffset,
    HostMemoryMap::VIF1recOffset + HostMemoryMap::VIF1recSize};
constexpr MtvuJitRange SharedVifCode{HostMemoryMap::VIFUnpackRecOffset,
    HostMemoryMap::VIFUnpackRecOffset + HostMemoryMap::VIFUnpackRecSize};

bool mtvu_write_range(size_t offset, size_t size)
{
    return (offset >= VuCode.offset && offset < VuCode.end && size <= VuCode.end - offset) ||
           (offset >= VifCode.offset && offset < VifCode.end && size <= VifCode.end - offset);
}

void remember_mtvu_handoff(size_t offset, size_t bytes)
{
    for (auto region : {VuCode, VifCode, SharedVifCode}) {
        const auto begin = std::max(offset, region.offset);
        const auto end = std::min(offset + bytes, region.end);
        if (begin < end) {
            s_mtvu_handoff.Add(begin, end);
            s_mtvu_handoff_pending.store(true, std::memory_order_release);
        }
    }
}

#ifdef IZPS2_WRAPPER_AUDIT
HostSys::JitCacheTiming s_publication;

void record_publication(uint64_t start, size_t bytes, size_t calls = 1)
{
    const uint64_t elapsed = iz_system_ticks() - start;
    s_publication.calls += calls;
    s_publication.bytes += bytes;
    s_publication.ticks += elapsed;
    if (elapsed > s_publication.max_ticks)
        s_publication.max_ticks = elapsed;
}
#endif

std::atomic_bool s_ee_warm_any_active;
struct EeWarmPublication
{
    Ps2EeWarmRanges ranges;
    size_t byte_budget;
    bool active;
};
constinit thread_local EeWarmPublication s_ee_warm{};

EeWarmPublication* active_ee_warm_ranges()
{
    // Avoid TLS resolution when no thread has an open EE batch.
    if (!s_ee_warm_any_active.load(std::memory_order_relaxed))
        return nullptr;
    auto& batch = s_ee_warm;
    return batch.active ? &batch : nullptr;
}

void drain_ee_warm_ranges(Ps2EeWarmRanges& pending)
{
    const auto ranges = pending.Pending();
    if (ranges.empty())
        return;
#ifdef IZPS2_WRAPPER_AUDIT
    size_t bytes = 0;
    for (const auto& range : ranges)
        bytes += range.size;
    const uint64_t start = iz_system_ticks();
#endif
    std::unique_lock<std::mutex> lock(s_sdk_jit_mutex, std::defer_lock);
    if (s_track_mtvu_jit)
        lock.lock();
    u32 result = 0;
    for (const auto& range : ranges)
    {
        result = s_cache_owner->publish(&s_cache, range.offset, range.size);
        if (result)
            break;
        if (s_track_mtvu_jit)
            remember_mtvu_handoff(range.offset, range.size);
    }
#ifdef IZPS2_WRAPPER_AUDIT
    record_publication(start, bytes, ranges.size());
#endif
    if (result)
    {
        Console.Error("IZPS2 EE warm publication failed: 0x%08x", result);
        std::abort();
    }
    pending.Reset();
}

size_t code_offset(const void* address)
{
    return reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(s_cache.rx);
}
}

void iz_track_mtvu_jit(bool enabled)
{
    s_track_mtvu_jit = enabled;
    s_mtvu_handoff.Clear();
    s_mtvu_handoff_pending.store(false, std::memory_order_relaxed);
}

bool iz_is_mtvu_worker() { return s_track_mtvu_jit && s_on_mtvu_worker; }
void iz_mtvu_worker_enter() { s_on_mtvu_worker = s_track_mtvu_jit; }
void iz_mtvu_worker_leave() { s_on_mtvu_worker = false; }

void iz_mtvu_worker_publish_handoff()
{
    if (!s_on_mtvu_worker || !s_mtvu_handoff_pending.load(std::memory_order_acquire))
        return;
    const std::lock_guard lock(s_sdk_jit_mutex);
    for (auto range : s_mtvu_handoff.Ranges()) {
        const auto size = range.end - range.offset;
#ifdef IZPS2_WRAPPER_AUDIT
        const auto start = iz_system_ticks();
#endif
        const auto rc = s_cache_owner->publish(&s_cache, range.offset, size);
        if (rc) {
            std::fprintf(stderr, "IZPS2 VU1 handoff publish offset=%zu rc=0x%08x\n", range.offset, rc);
            std::abort();
        }
#ifdef IZPS2_WRAPPER_AUDIT
        record_publication(start, size);
#endif
    }
    s_mtvu_handoff.Clear();
    s_mtvu_handoff_pending.store(false, std::memory_order_release);
}

static bool begin_ee_warm_ranges(size_t byte_budget)
{
    auto& batch = s_ee_warm;
    if (batch.active || !s_cache_size ||
        !s_cache_owner || s_cache_thread != std::this_thread::get_id())
        return false;
    batch.byte_budget = byte_budget;
    batch.active = true;
    s_ee_warm_any_active.store(true, std::memory_order_relaxed);
    return true;
}

bool iz_ps2_begin_ee_warm_ranges()
{
    return begin_ee_warm_ranges(PS2_EE_WARM_MAX_BYTES);
}

bool iz_ps2_begin_ee_checkpoint_ranges()
{
    // An ordered checkpoint seed is compiled with EE paused until the
    // enclosing scope drains. Only contiguous or overlapping writes coalesce.
    return begin_ee_warm_ranges(128 * 1024);
}

void iz_ps2_end_ee_warm_ranges()
{
    auto& batch = s_ee_warm;
    drain_ee_warm_ranges(batch.ranges);
    batch.active = false;
    s_ee_warm_any_active.store(false, std::memory_order_relaxed);
}

void iz_reserve_jit_worker_scratch(bool enabled)
{
    s_worker_scratch_requested = enabled;
}

Ps2JitWorkerScratch iz_jit_worker_scratch()
{
    if (!s_worker_scratch_requested || !s_cache_size || s_cache_thread != std::this_thread::get_id())
        return {};
    return {&s_cache, s_cache_size};
}

void iz_retain_jit_worker_scratch()
{
    s_worker_scratch_retained = true;
}

void HostSys::SetJitCacheApi(const Sb4JitCacheApi* api)
{
    s_jit_api = api;
}

#ifdef IZPS2_WRAPPER_AUDIT
HostSys::JitCacheTiming HostSys::GetJitCacheTiming()
{
    if (!s_track_mtvu_jit)
        return s_publication;
    const std::lock_guard lock(s_sdk_jit_mutex);
    return s_publication;
}
#endif

void* HostSys::AllocateJitCache(size_t size)
{
    if (!s_jit_api || !s_jit_api->open || !s_jit_api->publish || !s_jit_api->close)
        return nullptr;

    if (s_cache.owner)
    {
        ReleaseJitCache();
        if (s_cache.owner)
            return nullptr;
    }

    s_cache_owner = s_jit_api;
    const auto reserved = s_worker_scratch_requested ? size + WorkerScratchBytes : size;
    const uint32_t result = s_cache_owner->open(&s_cache, reserved);
    if (result)
    {
        Console.Warning("IZPS2 JIT cache initialization failed: 0x%08x", result);
        std::fprintf(stderr, "IZPS2_JIT_OPEN bytes=%zu rc=0x%08x\n", size, result);
        // A partial open may still own mappings. Retry cleanup without losing
        // the provider or overwriting the original Result in the open log.
        ReleaseJitCache();
        return nullptr;
    }

    s_cache_size = size;
    s_cache_thread = std::this_thread::get_id();
#ifdef IZPS2_WRAPPER_AUDIT
    s_publication = {};
#endif
    Console.WriteLn("IZPS2 JIT cache ready: %zu bytes", size);
    std::fprintf(stderr, "IZPS2_JIT_OPEN bytes=%zu rc=0x00000000\n", size);
    return s_cache.rx;
}

void HostSys::ReleaseJitCache()
{
    if (s_worker_scratch_retained) {
        std::fprintf(stderr, "IZPS2 JIT arena retained for unjoined test worker\n");
        return;
    }
    // A failed close retains the SDK owner for a subsequent close attempt.
#ifdef IZPS2_WRAPPER_AUDIT
    if (s_cache_size)
    {
        std::fprintf(stderr,
            "IZPS2_JIT_PUBLICATION calls=%llu bytes=%llu ticks=%llu max_ticks=%llu hz=%llu\n",
            static_cast<unsigned long long>(s_publication.calls),
            static_cast<unsigned long long>(s_publication.bytes),
            static_cast<unsigned long long>(s_publication.ticks),
            static_cast<unsigned long long>(s_publication.max_ticks),
            static_cast<unsigned long long>(iz_tick_frequency()));
    }
#endif
    s_cache_size = 0;
    if (s_cache.owner)
    {
        const uint32_t result = s_cache_owner->close(&s_cache);
        std::fprintf(stderr, "IZPS2_JIT_CLOSE rc=0x%08x\n", result);
        if (result)
            Console.Error("IZPS2 JIT cache close failed: 0x%08x", result);
    }
}

void* HostSys::GetWritableCodeAddress(void* address)
{
    const size_t offset = code_offset(address);
    const bool worker = iz_is_mtvu_worker();
    const bool foreign = worker ? !mtvu_write_range(offset, 1) :
        offset < s_cache_size && s_cache_thread != std::this_thread::get_id();
    if (foreign)
    {
        std::fprintf(stderr, "IZPS2_JIT_WRITE fatal=wrong-thread offset=%zu\n", offset);
        std::abort();
    }
    return offset < s_cache_size ? s_cache.rw + offset : address;
}

void HostSys::MemProtect(void* p,size_t n,const PageProtectionMode& mode){
    if(iz_memory_protect(p,n,mode.CanRead(),mode.CanWrite(),mode.CanExecute()))
        pxFailRel("libounce memory protection failed");
}
std::string HostSys::GetFileMappingName(const char* prefix){return prefix;}
void* HostSys::CreateSharedMemory(const char*,size_t){errno=ENOTSUP;return nullptr;}
void HostSys::DestroySharedMemory(void*){pxFailRel("No shared mappings in interpreter build");}
size_t HostSys::GetRuntimePageSize(){return 4096;}
size_t HostSys::GetRuntimeCacheLineSize(){return 64;}
void HostSys::FlushInstructionCache(void* address, u32 size)
{
    if (!size)
        return;

    const size_t offset = code_offset(address);
    const bool worker = iz_is_mtvu_worker();
    if (worker && !mtvu_write_range(offset, size)) {
        std::fprintf(stderr, "IZPS2 VU1 foreign publish offset=%zu bytes=%u\n", offset, size);
        std::abort();
    }
    if (auto* batch = active_ee_warm_ranges())
    {
        auto& pending = batch->ranges;
        const bool valid = offset < s_cache_size && size <= s_cache_size - offset;
        if (valid && pending.Add(offset, size, batch->byte_budget))
            return;
        drain_ee_warm_ranges(pending);
        if (valid && pending.Add(offset, size, batch->byte_budget))
            return;
    }
    if (offset >= s_cache_size)
    {
        iz_flush_instructions(address, size);
        return;
    }

    const auto publish = [&] {
#ifdef IZPS2_WRAPPER_AUDIT
        const auto start = iz_system_ticks();
#endif
        const auto result = s_cache_owner->publish(&s_cache, offset, size);
#ifdef IZPS2_WRAPPER_AUDIT
        record_publication(start, size);
#endif
        return result;
    };
    uint32_t result;
    if (s_track_mtvu_jit) {
        const std::lock_guard lock(s_sdk_jit_mutex);
        result = publish();
        if (!result && !worker)
            remember_mtvu_handoff(offset, size);
    } else {
        result = publish();
    }
    if (result)
    {
        Console.Error("IZPS2 JIT publication failed at 0x%zx: 0x%08x", offset, result);
        std::fprintf(stderr, "IZPS2_JIT_PUBLISH offset=%zu bytes=%u rc=0x%08x\n", offset, size, result);
        // An active recompiler cannot resume into partially published code.
        std::abort();
    }
}
SharedMemoryMappingArea::SharedMemoryMappingArea(u8* p,size_t n,size_t pages):m_base_ptr(p),m_size(n),m_num_pages(pages){}
SharedMemoryMappingArea::~SharedMemoryMappingArea(){pxAssert(m_num_mappings==0);}
std::unique_ptr<SharedMemoryMappingArea> SharedMemoryMappingArea::Create(size_t,bool,uptr){errno=ENOTSUP;return nullptr;}
u8* SharedMemoryMappingArea::Map(void*,size_t,void*,size_t,const PageProtectionMode&){errno=ENOTSUP;return nullptr;}
bool SharedMemoryMappingArea::Unmap(void*,size_t,bool){errno=ENOTSUP;return false;}
bool PageFaultHandler::Install(Error* e){Error::SetStringView(e,"Switch 2 interpreter mode has no fastmem fault handler");return false;}
bool PageFaultHandler::InstallSecondaryThread(){return false;}




void iz_ps2_flush_jit_pair(void* first, u32 first_size, void* second, u32 second_size)
{
    HostSys::FlushInstructionCache(first, first_size);
    HostSys::FlushInstructionCache(second, second_size);
}
