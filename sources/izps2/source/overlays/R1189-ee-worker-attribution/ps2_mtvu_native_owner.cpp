#include "ps2_sb4_session.h"
#include "ps2_mtvu_jit.h"
#include "ps2_run_domains.hpp"
#include <sb4/jit-cache.h>
extern "C" {
#include <switch/kernel/thread.h>
#include <switch/kernel/svc.h>
}

#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>

extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);

namespace {
enum class Phase : unsigned { Starting, Ready, Stopped };
struct NativeVuOwner {
    Ps2Sb4Worker* module = nullptr;
    const Sb4JitCacheApi* jit = nullptr;
    const Sb4Compat* compat = nullptr;
    size_t capacity = 0;
    void (*run)(void*) = nullptr;
    void* context = nullptr;
    Thread thread{};
    Sb4JitCache arena{};
    std::atomic<Phase> phase{Phase::Stopped};
    uint32_t open_result = 0;
    uint32_t publish_result = 0;
    uint32_t close_result = 0;
    uint32_t cpu = 0;
    bool executed = false;
    bool owns_thread = false;
    decltype(&threadClose) close_thread = nullptr;
};
NativeVuOwner owner;
constinit thread_local bool on_owner = false;
constexpr uint64_t JoinTimeoutNs = 4000000000ULL;

void trace(const char* stage, uint32_t value) {
    char line[136];
    const int length = std::snprintf(line, sizeof(line), "20 MTVU isolated %s value=0x%x\n", stage, value);
    if (length > 0 && static_cast<size_t>(length) < sizeof(line))
        izps2_boot_stage(nullptr, line, static_cast<uint32_t>(length));
}

[[noreturn]] void retain(const char* reason, uint32_t value) {
    owner.module->platform_retained = 1;
    trace(reason, value);
    for (;;)
        owner.compat->sleep_ns(1000000000);
}

void join_owner() {
    const auto waited = svcWaitSynchronizationSingle(owner.thread.handle, JoinTimeoutNs);
    if (waited)
        retain("wait-retained", waited);
    if (owner.phase.load(std::memory_order_acquire) != Phase::Stopped || owner.arena.owner)
        retain("arena-retained", owner.close_result);
    const auto closed = owner.close_thread(&owner.thread);
    if (closed)
        retain("thread-retained", closed);
    owner.owns_thread = false;
    owner.thread = {};
    trace("joined", 0);
}

void run_native(void*) {
    on_owner = true;
    owner.cpu = svcGetCurrentProcessorNumber();
    owner.open_result = owner.jit->open(&owner.arena, owner.capacity);
    if (!owner.open_result && owner.arena.owner && owner.arena.rw && owner.arena.rx &&
        owner.arena.rw != owner.arena.rx) {
        // Reserved tail lies outside VU1 and VIF1. Prove actual instruction
        // visibility before exposing the address to either CPU provider.
        constexpr uint32_t return17[] = {0xd503245f, 0x52800220, 0xd65f03c0};
        constexpr uint32_t return23 = 0x528002e0;
        const size_t offset = owner.capacity - 64;
        auto exec = reinterpret_cast<uint32_t (*)()>(owner.arena.rx + offset);
        std::memcpy(owner.arena.rw + offset, return17, sizeof(return17));
        owner.publish_result = owner.jit->publish(&owner.arena, offset, sizeof(return17));
        if (!owner.publish_result && exec() == 17) {
            std::memcpy(owner.arena.rw + offset + sizeof(uint32_t), &return23, sizeof(return23));
            owner.publish_result = owner.jit->publish(&owner.arena, offset + sizeof(uint32_t), sizeof(return23));
            owner.executed = !owner.publish_result && exec() == 23;
        }
    }
    if (owner.executed) {
        owner.phase.store(Phase::Ready, std::memory_order_release);
        owner.run(owner.context);
    }
    if (owner.arena.owner)
        owner.close_result = owner.jit->close(&owner.arena);
    on_owner = false;
    owner.phase.store(Phase::Stopped, std::memory_order_release);
}
}

void iz_mtvu_configure(Ps2Sb4Worker* worker, const Sb4JitCacheApi* api, size_t bytes) {
    owner.module = worker;
    owner.jit = api;
    owner.capacity = bytes;
    owner.compat = worker ? sb4ModuleCompat(worker->app) : nullptr;
}

bool iz_mtvu_requested() { return owner.module && owner.jit && owner.compat; }
bool iz_is_mtvu_worker() { return iz_mtvu_requested() && on_owner; }

bool iz_mtvu_open_native(void (*run)(void*), void* context) {
    if (!iz_mtvu_requested() || !owner.jit->open || !owner.jit->publish || !owner.jit->close ||
        !owner.compat->resolve_c || !owner.compat->sleep_ns)
        return false;
    const auto create = reinterpret_cast<decltype(&threadCreate)>(owner.compat->resolve_c("threadCreate"));
    const auto start = reinterpret_cast<decltype(&threadStart)>(owner.compat->resolve_c("threadStart"));
    owner.close_thread = reinterpret_cast<decltype(&threadClose)>(owner.compat->resolve_c("threadClose"));
    if (!create || !start || !owner.close_thread)
        return false;

    owner.run = run;
    owner.context = context;
    owner.open_result = owner.publish_result = owner.close_result = 0;
    owner.executed = false;
    const auto ee_core = svcGetCurrentProcessorNumber();
    uint64_t allowed = 0;
    if (svcGetInfo(&allowed, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))
        return false;
    // The pinned SDK header documents native threadCreate IDs 0..3.
    unsigned other = static_cast<unsigned>(allowed) & 0xf;
    if (ee_core < 4)
        other &= ~(1u << ee_core);
    if (!other)
        return false;
    const auto created = create(&owner.thread, run_native, nullptr, nullptr, 1024 * 1024, 0x3b, std::countr_zero(other));
    if (created) {
        if (owner.thread.handle)
            retain("create-retained", created);
        trace("create-failed", created);
        return false;
    }
    if (!owner.thread.handle)
        retain("create-missing-handle", 0);
    owner.phase.store(Phase::Starting, std::memory_order_relaxed);
    const auto started = start(&owner.thread);
    if (started) {
        const auto closed = owner.close_thread(&owner.thread);
        if (closed)
            retain("unstarted-thread-retained", closed);
        owner.thread = {};
        owner.phase.store(Phase::Stopped, std::memory_order_relaxed);
        trace("start-failed", started);
        return false;
    }
    owner.owns_thread = true;
    for (unsigned ms = 0; ms < 4000; ++ms) {
        const auto phase = owner.phase.load(std::memory_order_acquire);
        if (phase == Phase::Ready) {
            trace("ready-core", owner.cpu);
            return true;
        }
        if (phase == Phase::Stopped) {
            trace("qualify-open", owner.open_result);
            trace("qualify-publish", owner.publish_result);
            join_owner();
            return false;
        }
        owner.compat->sleep_ns(1000000);
    }
    retain("startup-retained", 0);
}

bool iz_mtvu_native_is_open() { return owner.owns_thread; }

Ps2WorkerCpuTicks iz_ps2_vu1_cpu_ticks()
{
    // The EE owns the handle and samples it before the worker is reaped.
    if (!owner.owns_thread || owner.phase.load(std::memory_order_acquire) != Phase::Ready)
        return {};
    u64 ticks = 0;
    const auto result = svcGetInfo(&ticks, InfoType_ThreadTickCount, owner.thread.handle, TickCountInfo_Total);
    return {ticks, owner.thread.handle, result};
}

int iz_mtvu_native_core() {
    if (owner.phase.load(std::memory_order_acquire) != Phase::Ready)
        return -1;
    return static_cast<int>(owner.cpu);
}

void iz_mtvu_close_native() {
    if (owner.owns_thread)
        join_owner();
}

MtvuCodeView iz_mtvu_code_view() {
    if (owner.phase.load(std::memory_order_acquire) != Phase::Ready)
        return {};
    return {owner.arena.rw, owner.arena.rx, owner.capacity - 4096};
}

uint32_t iz_mtvu_publish_on_owner(uint32_t offset, uint32_t bytes) {
    if (!on_owner || !bytes || offset >= owner.capacity - 4096 || bytes > owner.capacity - 4096 - offset)
        retain("publish-owner-or-range", offset);
    return owner.jit->publish(&owner.arena, offset, bytes);
}
