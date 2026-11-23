#include "ps2_iop_arena_probe.hpp"

#include "ps2_mtvu_jit.h"
#include "ps2_sb4_session.h"
#include <sb4/jit-cache.h>
extern "C" {
#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
}

#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);

namespace {
constexpr uint64_t JoinTimeoutNs = 4'000'000'000;
constexpr size_t ProbePageBytes = 4096;

struct Probe {
    const Sb4JitCacheApi& api;
    size_t bytes;
    int expected_core;
    Thread thread{};
    Sb4JitCache arena{};
    uint32_t open_rc = 0, publish_rc = 0, close_rc = 0;
    int core = -1;
    bool executed = false;
    std::atomic_bool finished{false};
};

void publish_probe(Probe& probe)
{
    // Identical return-constant check to the earlier VU1 qualification.
    // Only the private tail page is touched. No guest JIT code runs here.
    constexpr uint32_t initial[] = {0xd503245f, 0x52800220, 0xd65f03c0};
    constexpr uint32_t changed = 0x528002e0;
    const size_t offset = probe.bytes - 64;
    auto run = reinterpret_cast<uint32_t (*)()>(probe.arena.rx + offset);
    std::memcpy(probe.arena.rw + offset, initial, sizeof(initial));
    probe.publish_rc = probe.api.publish(&probe.arena, offset, sizeof(initial));
    if (probe.publish_rc || run() != 17)
        return;
    std::memcpy(probe.arena.rw + offset + sizeof(uint32_t), &changed, sizeof(changed));
    probe.publish_rc = probe.api.publish(&probe.arena, offset + sizeof(uint32_t), sizeof(changed));
    probe.executed = !probe.publish_rc && run() == 23;
}

void probe_on_owner(void* context)
{
    auto& probe = *static_cast<Probe*>(context);
    probe.core = static_cast<int>(svcGetCurrentProcessorNumber());
    if (probe.core == probe.expected_core) {
        probe.open_rc = probe.api.open(&probe.arena, probe.bytes);
        if (!probe.open_rc && probe.arena.owner && probe.arena.rw && probe.arena.rx &&
            probe.arena.rw != probe.arena.rx)
            publish_probe(probe);
        if (probe.arena.owner)
            probe.close_rc = probe.api.close(&probe.arena);
    }
    probe.finished.store(true, std::memory_order_release);
}

void trace(const char* reason)
{
    char line[106];
    const int length = std::snprintf(line, sizeof(line), "20 IOP isolated probe skipped=%s\n", reason);
    if (length > 0 && static_cast<size_t>(length) < sizeof(line))
        izps2_boot_stage(nullptr, line, static_cast<uint32_t>(length));
}
}

bool iz_probe_iop_arena(Ps2Sb4Worker& worker, const Sb4JitCacheApi* api, size_t bytes)
{
    const int vu_core = iz_mtvu_native_core();
    if (vu_core < 0 || vu_core >= 4 || !iz_mtvu_native_is_open()) {
        trace("vu1-owner-unavailable");
        return true;
    }

    const int ee_core = static_cast<int>(svcGetCurrentProcessorNumber());
    uint64_t allowed = 0;
    if (ee_core < 0 || ee_core >= 4 || ee_core == vu_core ||
        svcGetInfo(&allowed, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) {
        trace("core-placement-unknown");
        return true;
    }
    // The pinned libounce threadCreate header documents processor IDs 0..3.
    const unsigned free = static_cast<unsigned>(allowed) & 0xf & ~(1u << ee_core) & ~(1u << vu_core);
    if (!free) {
        trace("no-third-permitted-core");
        return true;
    }

    const auto* compat = sb4ModuleCompat(worker.app);
    if (!compat || !compat->resolve_c || !api || !api->open || !api->publish || !api->close ||
        bytes < ProbePageBytes || bytes % ProbePageBytes != 0) {
        trace("sdk-unavailable");
        return true;
    }
    const auto create = reinterpret_cast<decltype(&threadCreate)>(compat->resolve_c("threadCreate"));
    const auto start = reinterpret_cast<decltype(&threadStart)>(compat->resolve_c("threadStart"));
    const auto close = reinterpret_cast<decltype(&threadClose)>(compat->resolve_c("threadClose"));
    if (!create || !start || !close) {
        trace("thread-api-unavailable");
        return true;
    }

    const int iop_core = std::countr_zero(free);
    auto probe = std::unique_ptr<Probe>(new (std::nothrow) Probe{*api, bytes, iop_core});
    if (!probe) {
        trace("allocation-failed");
        return true;
    }

    const auto created = create(&probe->thread, probe_on_owner, probe.get(), nullptr,
        256 * 1024, 0x3b, iop_core);
    const bool has_handle = probe->thread.handle != 0;
    const bool can_start = !created && has_handle;
    const auto started = can_start ? start(&probe->thread) : 0;
    const bool ran = can_start && !started;
    const auto waited = ran ? svcWaitSynchronizationSingle(probe->thread.handle, JoinTimeoutNs) : 0;
    const bool joined = ran && !waited && probe->finished.load(std::memory_order_acquire);
    const auto closed = can_start && (!ran || joined) ? close(&probe->thread) : 0;
    const bool ambiguous_create = (created == 0) != has_handle;
    const bool retained = ambiguous_create || (ran && !joined) || closed ||
        (joined && (probe->arena.owner || probe->close_rc));

    char line[352];
    const int length = std::snprintf(line, sizeof(line),
        "20 IOP isolated probe bytes=%zu ee_core=%d vu1_core=%d requested_core=%d actual_core=%d "
        "create=0x%x start=0x%x wait=0x%x open=0x%x publish=0x%x close_arena=0x%x close_thread=0x%x "
        "executed=%u retained=%u\n",
        bytes, ee_core, vu_core, iop_core, joined ? probe->core : -1, created, started, waited,
        joined ? probe->open_rc : 0, joined ? probe->publish_rc : 0, joined ? probe->close_rc : 0, closed,
        unsigned(joined && probe->executed), unsigned(retained));
    if (length > 0 && static_cast<size_t>(length) < sizeof(line))
        izps2_boot_stage(nullptr, line, static_cast<uint32_t>(length));
    if (retained) {
        worker.platform_retained = 1;
        probe.release(); // A thread or mapping still owns the allocation.
    }
    return !retained;
}
