#include "ps2_sb4_session.h"
#include <sb4/jit-cache.h>
extern "C" {
#include <switch/kernel/thread.h>
#include <switch/kernel/svc.h>
}
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);

namespace {
struct Vu1JitProbe {
    const Sb4JitCacheApi* api;
    size_t bytes;
    Thread worker{};
    Sb4JitCache arena{};
    std::atomic<bool> finished{false};
    uint32_t opened = 0;
    uint32_t published = 0;
    uint32_t closed = 0;
    bool qualified = false;
};

void qualify_vu1_jit(void* context)
{
    auto& probe = *static_cast<Vu1JitProbe*>(context);
    auto& arena = probe.arena;
    probe.opened = probe.api->open(&arena, probe.bytes);
    if (!probe.opened && arena.owner && arena.rw && arena.rx && arena.rw != arena.rx) {
        using ReturnConstant = uint32_t (*)();
        constexpr uint32_t mov_23 = 0x528002e0;
        constexpr uint32_t function[] = {0xd503245f, 0x52800220, 0xd65f03c0}; // BTI c, MOV W0,#17, RET
        const size_t offset = probe.bytes - 64;
        auto code = reinterpret_cast<ReturnConstant>(arena.rx + offset);
        std::memcpy(arena.rw + offset, function, sizeof(function));
        probe.published = probe.api->publish(&arena, offset, sizeof(function));
        if (!probe.published && code() == 17) {
            std::memcpy(arena.rw + offset + sizeof(uint32_t), &mov_23, sizeof(mov_23));
            probe.published = probe.api->publish(&arena, offset + sizeof(uint32_t), sizeof(mov_23));
            probe.qualified = !probe.published && code() == 23;
        }
    }
    if (arena.owner)
        probe.closed = probe.api->close(&arena);
    probe.finished.store(true, std::memory_order_release);
}

} // namespace

bool probe_vu1_jit(Ps2Sb4Worker* worker, const Sb4JitCacheApi* api, size_t bytes)
{
    const auto* compat = sb4ModuleCompat(worker->app);
    if (!compat || !api || !api->open || !api->publish || !api->close)
        return true;
    const auto create = reinterpret_cast<decltype(&threadCreate)>(compat->resolve_c("threadCreate"));
    const auto start = reinterpret_cast<decltype(&threadStart)>(compat->resolve_c("threadStart"));
    const auto close = reinterpret_cast<decltype(&threadClose)>(compat->resolve_c("threadClose"));
    if (!create || !start || !close)
        return true;
    auto probe = std::unique_ptr<Vu1JitProbe>(new (std::nothrow) Vu1JitProbe{api, bytes});
    if (!probe)
        return true;
    constexpr char begin[] = "20 secondary VU1 JIT probe begin\n";
    izps2_boot_stage(nullptr, begin, sizeof(begin) - 1);
    Result created = create(&probe->worker, qualify_vu1_jit, probe.get(), nullptr, 256 * 1024, 0x3b, -2);
    const bool launch_attempted = created == 0;
    Result started = launch_attempted ? start(&probe->worker) : 0;
    const bool ran = launch_attempted && started == 0;
    Result waited = ran ? svcWaitSynchronizationSingle(probe->worker.handle, 4000000000ULL) : 0;
    const bool finished = ran && !waited && probe->finished.load(std::memory_order_acquire);
    const bool unknown_create = created && probe->worker.handle;
    Result closed = !created && probe->worker.handle && (!ran || finished) ? close(&probe->worker) : 0;
    const bool retained = unknown_create || (ran && !finished) || closed || (finished && probe->arena.owner);
    char result[240];
    const int length = std::snprintf(result, sizeof(result),
        "20 secondary VU1 JIT create=0x%x start=0x%x wait=0x%x open=0x%x publish=0x%x close_arena=0x%x "
        "close_thread=0x%x executed=%u retained=%u\n", created, started, waited,
        finished ? probe->opened : 0, finished ? probe->published : 0, finished ? probe->closed : 0,
        closed, unsigned(finished && probe->qualified), unsigned(retained));
    if (length > 0 && static_cast<size_t>(length) < sizeof(result))
        izps2_boot_stage(nullptr, result, static_cast<uint32_t>(length));
    if (retained) {
        worker->platform_retained = 1;
        probe.release(); // SDK or worker still owns the object and its mappings.
    }
    return !retained;
}
