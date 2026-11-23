#include "ps2_ee_warm_ranges.h"
#include "ps2_mtvu_jit.h"
#include "ps2_iop_jit_owner.hpp"
#include "ps2_publication_timing.hpp"
#include "Memory.h"
#include "MTVU.h"
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
// The primary code arena is owned by EE. When admitted, the native MTVU
// thread owns an independent SDK arena for VU1 and VIF1.
const Sb4JitCacheApi* s_jit_api;
const Sb4JitCacheApi* s_cache_owner;
Sb4JitCache s_cache;
size_t s_cache_size;
std::thread::id s_cache_thread;
std::mutex s_provider_publish_mutex;

bool mapped_code_offset(const MtvuCodeView& view, const void* address, size_t& offset)
{
    offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(view.rx);
    return offset < view.bytes;
}

#ifdef IZPS2_WRAPPER_AUDIT
// The EE owner takes these snapshots during code generation. Different SDK
// arenas share the provider lock, but the owner-local metrics do not.
HostSys::JitCacheTiming s_primary_publication;
uint64_t s_primary_publish_wait;
struct alignas(64) WorkerPublication {
    std::atomic<uint64_t> ticks{0};
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> wait_ticks{0};
} s_vu1_publication;

void record_primary_publication(uint64_t start, size_t bytes, size_t calls = 1)
{
    const uint64_t elapsed = iz_system_ticks() - start;
    s_primary_publication.calls += calls;
    s_primary_publication.bytes += bytes;
    s_primary_publication.ticks += elapsed;
    if (elapsed > s_primary_publication.max_ticks)
        s_primary_publication.max_ticks = elapsed;
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
    const uint64_t wait_start = iz_system_ticks();
#endif
    const std::lock_guard lock(s_provider_publish_mutex);
#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t start = iz_system_ticks();
    s_primary_publish_wait += start - wait_start;
#endif
    u32 result = 0;
    for (const auto& range : ranges)
    {
        result = s_cache_owner->publish(&s_cache, range.offset, range.size);
        if (result)
            break;
    }
#ifdef IZPS2_WRAPPER_AUDIT
    record_primary_publication(start, bytes, ranges.size());
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

std::mutex& iz_ps2_jit_provider_lock()
{
    return s_provider_publish_mutex;
}

void HostSys::SetJitCacheApi(const Sb4JitCacheApi* api)
{
    s_jit_api = api;
}

#ifdef IZPS2_WRAPPER_AUDIT
HostSys::JitCacheTiming HostSys::GetJitCacheTiming()
{
    return s_primary_publication;
}

Ps2PublicationTiming ps2_publication_timing()
{
    return {s_primary_publish_wait, s_vu1_publication.calls.load(std::memory_order_relaxed),
            s_vu1_publication.ticks.load(std::memory_order_relaxed),
            s_vu1_publication.wait_ticks.load(std::memory_order_relaxed)};
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
    const uint32_t result = s_cache_owner->open(&s_cache, size);
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
    s_primary_publication = {};
    s_primary_publish_wait = 0;
    s_vu1_publication.ticks.store(0, std::memory_order_relaxed);
    s_vu1_publication.calls.store(0, std::memory_order_relaxed);
    s_vu1_publication.wait_ticks.store(0, std::memory_order_relaxed);
#endif
    if (iz_mtvu_requested())
        vu1Thread.Open();
    Console.WriteLn("IZPS2 JIT cache ready: %zu bytes", size);
    std::fprintf(stderr, "IZPS2_JIT_OPEN bytes=%zu rc=0x00000000\n", size);
    return s_cache.rx;
}

void HostSys::ReleaseJitCache()
{
    if (iz_mtvu_native_is_open())
        vu1Thread.Close();
    // A failed close retains the SDK owner for a subsequent close attempt.
#ifdef IZPS2_WRAPPER_AUDIT
    if (s_cache_size)
    {
        std::fprintf(stderr,
            "IZPS2_JIT_PUBLICATION calls=%llu bytes=%llu ticks=%llu max_ticks=%llu hz=%llu\n",
            static_cast<unsigned long long>(s_primary_publication.calls),
            static_cast<unsigned long long>(s_primary_publication.bytes),
            static_cast<unsigned long long>(s_primary_publication.ticks),
            static_cast<unsigned long long>(s_primary_publication.max_ticks),
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

u8* iz_mtvu_redirect_code(size_t primary_offset)
{
    if (!iz_mtvu_requested())
        return nullptr;
    const auto view = iz_mtvu_code_view();
    if (!view.rx)
        return nullptr;
    if (primary_offset >= HostMemoryMap::mVU1recOffset &&
        primary_offset - HostMemoryMap::mVU1recOffset < HostMemoryMap::mVU1recSize)
        return view.rx + (primary_offset - HostMemoryMap::mVU1recOffset);
    if (primary_offset >= HostMemoryMap::VIF1recOffset &&
        primary_offset - HostMemoryMap::VIF1recOffset < HostMemoryMap::VIF1recSize)
        return view.rx + HostMemoryMap::mVU1recSize + (primary_offset - HostMemoryMap::VIF1recOffset);
    return nullptr;
}

void iz_mtvu_publish_packet(u32 offset, u32 bytes)
{
#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t wait_start = iz_system_ticks();
#endif
    const std::lock_guard lock(s_provider_publish_mutex);
#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t start = iz_system_ticks();
#endif
    const auto result = iz_mtvu_publish_on_owner(offset, bytes);
    if (result) {
        std::fprintf(stderr, "IZPS2 MTVU JIT publish failed offset=%u bytes=%u rc=0x%08x\n", offset, bytes, result);
        std::abort();
    }
#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t elapsed = iz_system_ticks() - start;
    s_vu1_publication.ticks.fetch_add(elapsed, std::memory_order_relaxed);
    s_vu1_publication.calls.fetch_add(1, std::memory_order_relaxed);
    s_vu1_publication.wait_ticks.fetch_add(start - wait_start, std::memory_order_relaxed);
#endif
}

void* HostSys::GetWritableCodeAddress(void* address)
{
    if (iz_iop_jit_enabled()) {
        const auto io = iz_iop_jit_view();
        size_t offset;
        if (io.contains(address, offset)) {
            if (!iz_iop_jit_can_write())
                std::abort();
            return io.rw + offset;
        }
        if (io.rx && iz_iop_is_jit_owner()) {
            std::fprintf(stderr, "IZPS2 IOP foreign JIT write\n");
            std::abort();
        }
    }
    const auto view = iz_mtvu_requested() ? iz_mtvu_code_view() : MtvuCodeView{};
    size_t secondary_offset;
    if (mapped_code_offset(view, address, secondary_offset)) {
        if (!iz_is_mtvu_worker()) {
            if (s_cache_thread != std::this_thread::get_id())
                std::abort();
            vu1Thread.WaitVU();
        }
        return view.rw + secondary_offset;
    }
    if (iz_is_mtvu_worker()) {
        std::fprintf(stderr, "IZPS2 MTVU foreign JIT write\n");
        std::abort();
    }
    const size_t offset = code_offset(address);
    if (offset < s_cache_size && s_cache_thread != std::this_thread::get_id())
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

    const bool isolated_iop = iz_iop_jit_enabled();
    if (isolated_iop) {
        const auto io = iz_iop_jit_view();
        size_t offset;
        if (io.contains(address, offset)) {
            if (size > io.bytes - offset)
                std::abort();
            iz_iop_jit_publish(static_cast<u32>(offset), size);
            return;
        }
        if (io.rx && iz_iop_is_jit_owner()) {
            std::fprintf(stderr, "IZPS2 IOP foreign JIT publication\n");
            std::abort();
        }
    }

    const auto view = iz_mtvu_requested() ? iz_mtvu_code_view() : MtvuCodeView{};
    size_t secondary_offset;
    if (mapped_code_offset(view, address, secondary_offset)) {
        if (size > view.bytes - secondary_offset)
            std::abort();
        if (iz_is_mtvu_worker()) {
            iz_mtvu_publish_packet(static_cast<u32>(secondary_offset), size);
            return;
        }
        if (s_cache_thread != std::this_thread::get_id())
            std::abort();
        vu1Thread.PublishCode(static_cast<u32>(secondary_offset), size);
        return;
    }
    if (iz_is_mtvu_worker()) {
        std::fprintf(stderr, "IZPS2 MTVU foreign JIT publication\n");
        std::abort();
    }
    const size_t offset = code_offset(address);
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

    std::unique_lock<std::mutex> lock(s_provider_publish_mutex, std::defer_lock);
    const bool shared_provider = isolated_iop || iz_mtvu_native_is_open();
#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t wait_start = shared_provider ? iz_system_ticks() : 0;
#endif
    if (shared_provider)
        lock.lock();
#ifdef IZPS2_WRAPPER_AUDIT
    const uint64_t start = iz_system_ticks();
    if (shared_provider)
        s_primary_publish_wait += start - wait_start;
#endif
    const uint32_t result = s_cache_owner->publish(&s_cache, offset, size);
#ifdef IZPS2_WRAPPER_AUDIT
    record_primary_publication(start, size);
#endif
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
