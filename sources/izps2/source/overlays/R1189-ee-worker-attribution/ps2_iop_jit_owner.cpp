#include "ps2_iop_jit_owner.hpp"
#include "ps2_run_domains.hpp"

#include "IopThread/IopThread.h"
#include "ps2_iop_jit_mailbox.hpp"
#include "ps2_mtvu_jit.h"
#include "ps2_sb4_session.h"

#include <sb4/jit-cache.h>
extern "C" {
// The SDK defines scalar 128-bit aliases. PCSX2's types with those
// names are aggregates. No selected SVC declaration uses either alias.
#define u128 Sb4NativeUint128
#define s128 Sb4NativeInt128
#include <switch/kernel/svc.h>
#include <switch/kernel/levent.h>
#include <switch/kernel/thread.h>
#undef s128
#undef u128
}

#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <thread>

extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);
std::mutex& iz_ps2_jit_provider_lock();

namespace {
constexpr size_t ProbePage = 4096;
constexpr uint64_t PublishTimeout = 4'000'000'000;
constexpr uint64_t ParkTimeout = 1'000'000;
constexpr uint64_t StopTimeout = 2'000'000'000;
enum class WorkerPhase : uint32_t { Starting, GuestLoop, Draining, Returning };
struct Owner {
    std::atomic<IopThread::Thread*> scheduler{nullptr};
    std::atomic<uint8_t*> rw{nullptr}, rx{nullptr};
    Ps2Sb4Worker* module = nullptr;
    const Sb4JitCacheApi* api = nullptr;
    const Sb4Compat* compat = nullptr;
    std::thread::id ee;
    std::atomic<size_t> bytes{0};
    int core = -1;
    Sb4JitCache cache{};
    uint32_t (*get_wait_handle)() = nullptr;
    decltype(&threadClose) close_thread = nullptr;
    void (*free_thread)(void*) = nullptr;
    ::Thread* native_thread = nullptr;
    Handle wait_handle{};
    std::atomic<WorkerPhase> phase{WorkerPhase::Starting};
};
Owner owner;
// The auto-clear event lives for the whole module. A notification racing
// shutdown cannot outlive it. Even a stale token only triggers a recheck.
constinit LEvent scheduler_wake{0, true};
constinit thread_local bool on_owner = false;
constinit thread_local bool scoped_create = false;
constinit thread_local ::Thread* reap_thread = nullptr;

::Thread* NativeThread(Handle expected)
{
    static_assert(offsetof(::Thread, handle) == 0);
    const auto address = reinterpret_cast<uintptr_t>(pthread_self());
    if (!expected || address < ProbePage || address == UINTPTR_MAX || address % alignof(::Thread))
        return nullptr;
    // The launcher binds pthread_self to the verified libounce SDK. Confirm
    // that its descriptor contains the independent host wait handle before
    // using the native close operation on that pointer.
    Handle handle;
    std::memcpy(&handle, reinterpret_cast<const void*>(address), sizeof(handle));
    return handle == expected ? reinterpret_cast<::Thread*>(address) : nullptr;
}

void Trace(const char* operation, uint32_t result)
{
    char line[120];
    const auto length = std::snprintf(line, sizeof(line),
        "20 IOP native %s rc=0x%08x\n", operation, result);
    if (length > 0 && static_cast<size_t>(length) < sizeof(line))
        izps2_boot_stage(nullptr, line, static_cast<uint32_t>(length));
}

[[noreturn]] void Retain(const char* operation, uint32_t result)
{
    owner.module->platform_retained = 1;
    Trace(operation, result);
    for (;;)
        owner.compat->sleep_ns(1'000'000'000);
}

uint32_t PublishOnOwner(void*, uint32_t offset, uint32_t bytes)
{
    const size_t arena_bytes = owner.bytes.load(std::memory_order_relaxed);
    if (!on_owner || !bytes || offset >= arena_bytes || bytes > arena_bytes - offset)
        Retain("wrong-publisher-or-range", offset);
    const std::lock_guard lock(iz_ps2_jit_provider_lock());
    return owner.api->publish(&owner.cache, offset, bytes);
}
}

bool iz_iop_jit_configure(Ps2Sb4Worker& module, const Sb4JitCacheApi* api,
    IopThread::Thread& scheduler, size_t bytes)
{
    const auto* compat = sb4ModuleCompat(module.app);
    if (owner.scheduler.load(std::memory_order_acquire) || !compat || !compat->sleep_ns || !compat->resolve_c ||
        !api || !api->open || !api->publish || !api->close || !bytes ||
        bytes % ProbePage || bytes > UINT32_MAX - ProbePage)
        return false;
    const auto ee_core = svcGetCurrentProcessorNumber();
    uint64_t available = 0;
    if (ee_core >= 4 || svcGetInfo(&available, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))
        return false;
    unsigned remaining = static_cast<unsigned>(available) & 0xf & ~(1u << ee_core);
    const int vu_core = iz_mtvu_native_core();
    if (vu_core >= 0 && vu_core < 4)
        remaining &= ~(1u << vu_core);
    if (!remaining)
        return false;

    const auto get_wait_handle = reinterpret_cast<uint32_t (*)()>(
        compat->resolve_c("sb4HostThreadWaitHandle"));
    const auto close_thread = reinterpret_cast<decltype(&threadClose)>(compat->resolve_c("threadClose"));
    const auto free_thread = reinterpret_cast<void (*)(void*)>(compat->resolve_c("free"));
    // RUN25's launcher uses a libounce Thread as the pthread descriptor.
    // Verify this on the caller and again inside the worker before accepting it.
    if (!get_wait_handle || !close_thread || !free_thread ||
        !NativeThread(get_wait_handle()))
        return false;
    owner.get_wait_handle = get_wait_handle;
    owner.close_thread = close_thread;
    owner.free_thread = free_thread;
    owner.module = &module;
    owner.api = api;
    owner.compat = compat;
    owner.ee = std::this_thread::get_id();
    owner.bytes.store(bytes, std::memory_order_relaxed);
    owner.core = std::countr_zero(remaining);
    owner.scheduler.store(&scheduler, std::memory_order_release);
    return true;
}

void iz_iop_jit_unconfigure()
{
    auto* scheduler = owner.scheduler.load(std::memory_order_acquire);
    if (!scheduler)
        return;
    if (scheduler->IsRunning() || owner.cache.owner)
        Retain("unconfigure-retained-owner", 0);
    owner.scheduler.store(nullptr, std::memory_order_release);
}

bool iz_iop_jit_enabled()
{
    return owner.scheduler.load(std::memory_order_acquire) != nullptr;
}

bool iz_iop_jit_selects_thread(const IopThread::Thread& scheduler)
{
    return owner.scheduler.load(std::memory_order_acquire) == &scheduler;
}

bool iz_iop_jit_attach(IopThread::Thread& scheduler)
{
    // A hold existing before Start keeps guest dispatch parked until the EE
    // replaces the dispatchers, constant pool and old code LUT entries.
    if (!scheduler.HoldDepth() ||
        !Ps2IopJitMailbox::Attach(scheduler, PublishOnOwner, nullptr))
        return false;
    owner.phase.store(WorkerPhase::Starting, std::memory_order_relaxed);
    owner.native_thread = nullptr;
    owner.wait_handle = 0;
    return true;
}

void iz_iop_jit_arm_scoped_create()
{
    scoped_create = true;
}

void iz_iop_jit_disarm_scoped_create()
{
    scoped_create = false;
}

extern "C" bool iz_ps2_iop_take_scoped_create()
{
    const bool armed = scoped_create;
    scoped_create = false;
    return armed;
}

void iz_iop_jit_detach(IopThread::Thread& scheduler)
{
    if (!Ps2IopJitMailbox::Detach(scheduler))
        Retain("detach-retained-command", 0);
}

bool iz_iop_jit_open_on_worker()
{
    const auto handle = owner.get_wait_handle();
    auto* current = NativeThread(handle);
    if (!current)
        return false;
    owner.native_thread = current;
    owner.wait_handle = handle;
    const auto result = svcSetThreadCoreMask(CUR_THREAD_HANDLE, owner.core, 1u << owner.core);
    if (result || svcGetCurrentProcessorNumber() != static_cast<uint32_t>(owner.core)) {
        Trace("affinity-refused", result);
        return false;
    }
    uint32_t opened;
    uint32_t published = 0;
    bool verified = false;
    {
        const std::lock_guard lock(iz_ps2_jit_provider_lock());
        const size_t arena_bytes = owner.bytes.load(std::memory_order_relaxed);
        opened = owner.api->open(&owner.cache, arena_bytes + ProbePage);
        if (!opened && owner.cache.owner && owner.cache.rw && owner.cache.rx &&
            owner.cache.rw != owner.cache.rx) {
            constexpr uint32_t function[] = {0xd503245f, 0x52800220, 0xd65f03c0};
            std::memcpy(owner.cache.rw + arena_bytes, function, sizeof(function));
            published = owner.api->publish(&owner.cache, arena_bytes, sizeof(function));
            auto leaf = reinterpret_cast<unsigned (*)()>(owner.cache.rx + arena_bytes);
            verified = !published && leaf() == 17;
        }
    }
    if (!verified) {
        Trace("open-refused", opened);
        Trace("publication-refused", published);
        return false;
    }
    on_owner = true;
    owner.phase.store(WorkerPhase::GuestLoop, std::memory_order_relaxed);
    owner.rw.store(owner.cache.rw, std::memory_order_relaxed);
    owner.rx.store(owner.cache.rx, std::memory_order_release);
    Trace("ready-core", static_cast<uint32_t>(owner.core));
    return true;
}

void iz_iop_jit_note_worker_drain()
{
    owner.phase.store(WorkerPhase::Draining, std::memory_order_release);
}

void iz_iop_jit_leave_worker()
{
    owner.rx.store(nullptr, std::memory_order_release);
    on_owner = false;
    owner.phase.store(WorkerPhase::Returning, std::memory_order_release);
}

void iz_iop_jit_park()
{
    // Recheck the scheduler predicate even if a notification is lost.
    leventWait(&scheduler_wake, ParkTimeout);
}

bool iz_iop_jit_notify(const IopThread::Thread& scheduler)
{
    if (!iz_iop_jit_selects_thread(scheduler))
        return false;
    leventSignal(&scheduler_wake);
    return true;
}

void iz_iop_jit_prepare_worker_reap(void* thread)
{
    // The descriptor and borrowed handle are published by Start's acknowledgement.
    if (!owner.native_thread || owner.native_thread != thread || !owner.wait_handle || reap_thread)
        Retain("reap-descriptor-mismatch", 0);
    Trace("kernel-wait-begin", owner.wait_handle);
    const auto result = svcWaitSynchronizationSingle(owner.wait_handle, StopTimeout);
    const auto phase = owner.phase.load(std::memory_order_acquire);
    Trace("exit-worker-phase", static_cast<uint32_t>(phase));
    if (result || phase != WorkerPhase::Returning)
        Retain("kernel-wait-retained", result);
    Trace("kernel-wait-returned", 0);
    reap_thread = owner.native_thread;
    Trace("scoped-reap-begin", 0);
}

extern "C" bool iz_ps2_iop_try_native_join(pthread_t thread, void** result)
{
    if (reinterpret_cast<void*>(thread) != reap_thread || !reap_thread)
        return false;
    // The kernel has already confirmed exit. Use the provider's close and
    // allocator directly, then let libsb4 clear this isolated receipt.
    Trace("native-close-begin", 0);
    if (const auto rc = owner.close_thread(reap_thread))
        Retain("native-close-retained", rc);
    Trace("native-close-returned", 0);
    owner.free_thread(reap_thread);
    reap_thread = nullptr;
    owner.native_thread = nullptr;
    owner.wait_handle = 0;
    if (result)
        *result = nullptr; // Threading::ThreadProc returns nullptr.
    Trace("native-descriptor-released", 0);
    return true;
}

void iz_iop_jit_reap_after_join()
{
    auto* scheduler = owner.scheduler.load(std::memory_order_acquire);
    if (!scheduler || scheduler->IsRunning() || owner.ee != std::this_thread::get_id() ||
        owner.rx.load(std::memory_order_acquire) || reap_thread || owner.native_thread)
        Retain("reap-before-worker-exit", 0);
    Trace("join-returned", 0);
    if (owner.cache.owner) {
        // The SDK close releases process mappings. All guest execution and
        // owner publication ended before the successful join.
        Trace("arena-close-begin", 0);
        uint32_t result;
        {
            const std::lock_guard lock(iz_ps2_jit_provider_lock());
            result = owner.api->close(&owner.cache);
        }
        if (result || owner.cache.owner)
            Retain("close-retained", result);
        Trace("arena-closed", 0);
    }
    owner.rw.store(nullptr, std::memory_order_relaxed);
}

bool iz_iop_is_jit_owner()
{
    return on_owner;
}

Ps2WorkerCpuTicks iz_ps2_iop_cpu_ticks()
{
    // Sampled only on EE, the same thread that clears the borrowed handle.
    if (!owner.rx.load(std::memory_order_acquire) || !owner.wait_handle)
        return {};
    u64 ticks = 0;
    const auto result = svcGetInfo(&ticks, InfoType_ThreadTickCount, owner.wait_handle, TickCountInfo_Total);
    return {ticks, owner.wait_handle, result};
}

bool iz_iop_jit_can_write()
{
    if (on_owner)
        return true;
    auto* scheduler = owner.scheduler.load(std::memory_order_acquire);
    return scheduler && owner.ee == std::this_thread::get_id() && scheduler->IsRunning() &&
        scheduler->IopIsStopped();
}

Ps2IopJitView iz_iop_jit_view()
{
    if (auto* rx = owner.rx.load(std::memory_order_acquire))
        return {rx, owner.rw.load(std::memory_order_relaxed), owner.bytes.load(std::memory_order_relaxed)};
    return {};
}

void iz_iop_jit_publish(uint32_t offset, uint32_t bytes)
{
    if (on_owner) {
        if (const auto result = PublishOnOwner(nullptr, offset, bytes))
            Retain("local-publication-retained", result);
        return;
    }
    if (!iz_iop_jit_can_write())
        Retain("unexpected-publication-owner", 0);
    auto* scheduler = owner.scheduler.load(std::memory_order_relaxed);
    const auto result = Ps2IopJitMailbox::PublishWhileParked(*scheduler, offset, bytes, PublishTimeout);
    if (result.outcome != Ps2IopJitMailbox::Outcome::Completed || result.sdk_result)
        Retain("remote-publication-retained", result.sdk_result ? result.sdk_result :
            0xe0000000u | static_cast<uint32_t>(result.outcome));
}
