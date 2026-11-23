#include "ps2_iop_jit_owner.hpp"

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
#undef s128
#undef u128
}

#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);
std::mutex& iz_ps2_jit_provider_lock();

namespace {
constexpr size_t ProbePage = 4096;
constexpr uint64_t PublishTimeout = 4'000'000'000;
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
};
Owner owner;
constinit thread_local bool on_owner = false;

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
    if (owner.scheduler.load(std::memory_order_acquire) || !compat || !compat->sleep_ns ||
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
    return scheduler.HoldDepth() > 0 &&
        Ps2IopJitMailbox::Attach(scheduler, PublishOnOwner, nullptr);
}

void iz_iop_jit_detach(IopThread::Thread& scheduler)
{
    if (!Ps2IopJitMailbox::Detach(scheduler))
        Retain("detach-retained-command", 0);
}

bool iz_iop_jit_open_on_worker()
{
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
        iz_iop_jit_close_on_worker();
        return false;
    }
    on_owner = true;
    owner.rw.store(owner.cache.rw, std::memory_order_relaxed);
    owner.rx.store(owner.cache.rx, std::memory_order_release);
    Trace("ready-core", static_cast<uint32_t>(owner.core));
    return true;
}

void iz_iop_jit_close_on_worker()
{
    owner.rx.store(nullptr, std::memory_order_release);
    if (owner.cache.owner) {
        uint32_t result;
        {
            const std::lock_guard lock(iz_ps2_jit_provider_lock());
            result = owner.api->close(&owner.cache);
        }
        if (result || owner.cache.owner)
            Retain("close-retained", result);
        Trace("arena-closed", 0);
    }
    on_owner = false;
}

bool iz_iop_is_jit_owner()
{
    return on_owner;
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
