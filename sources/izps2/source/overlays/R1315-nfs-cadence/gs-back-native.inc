// SPDX-License-Identifier: GPL-3.0+
#include "gs-back-native.h"
#include "ps2_sb4_session.h"

#include <sb4/native-services.h>
#include <sb4/storage-provider.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
extern "C" {
// libounce's scalar aliases differ from PCSX2's register aggregates.
#define u128 Sb4NativeUint128
#define s128 Sb4NativeInt128
#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
#undef s128
#undef u128
}

namespace GsBackNative
{
namespace
{
constexpr uint64_t ExitTimeoutNs = 10'000'000'000;

struct Worker
{
    Ps2Sb4Worker* module = nullptr;
    const Sb4Compat* compat = nullptr;
    decltype(&sb4SdCreateHostedThread) create = nullptr;
    decltype(&threadClose) close = nullptr;
    uint32_t (*self_handle)() = nullptr;
    void (*release_descriptor)(void*) = nullptr;
    void (*run)(void*) = nullptr;
    void* context = nullptr;
    pthread_t pthread{};
    uint64_t generation = 0;
    std::atomic<Handle> handle{0};
};
Worker worker;

[[noreturn]] void Retain(Phase phase)
{
    worker.module->platform_retained = 1;
    Progress.store(phase, std::memory_order_release);
    for (;;)
        worker.compat->sleep_ns(1'000'000'000);
}

::Thread* NativeDescriptor(pthread_t thread, Handle expected)
{
    static_assert(offsetof(::Thread, handle) == 0);
    const auto address = reinterpret_cast<uintptr_t>(thread);
    if (!expected || address < 4096 || address == UINTPTR_MAX || address % alignof(::Thread))
        return nullptr;

    Handle descriptor_handle{};
    std::memcpy(&descriptor_handle, reinterpret_cast<const void*>(address), sizeof(descriptor_handle));
    return descriptor_handle == expected ? reinterpret_cast<::Thread*>(address) : nullptr;
}

void* Entry(void*)
{
    worker.handle.store(worker.self_handle(), std::memory_order_release);
    worker.run(worker.context);
    return nullptr;
}
}

void Configure(Ps2Sb4Worker* module)
{
    pxAssertRel(!worker.pthread, "GS back worker is still owned");
    worker.module = module;
    const auto* compat = module ? sb4ModuleCompat(module->app) : nullptr;
    worker.compat = nullptr;
    Progress.store(Phase::Idle, std::memory_order_relaxed);
    if (!compat || compat->backend != SB4_COMPAT_LIBOUNCE || !compat->resolve_c || !compat->sleep_ns)
        return;

    const auto version = reinterpret_cast<uint32_t (*)()>(compat->resolve_c(SB4_NATIVE_SERVICES_IMPORT));
    if (!version || version() != SB4_NATIVE_SERVICES_VERSION)
        return;

    worker.create = reinterpret_cast<decltype(worker.create)>(compat->resolve_c("sb4SdCreateHostedThread"));
    worker.close = reinterpret_cast<decltype(worker.close)>(compat->resolve_c("threadClose"));
    worker.self_handle = reinterpret_cast<decltype(worker.self_handle)>(compat->resolve_c("sb4HostThreadWaitHandle"));
    worker.release_descriptor = reinterpret_cast<decltype(worker.release_descriptor)>(compat->resolve_c("free"));
    if (worker.create && worker.close && worker.self_handle && worker.release_descriptor &&
        NativeDescriptor(pthread_self(), worker.self_handle()))
        worker.compat = compat;
}

[[nodiscard]] static bool Start(void (*callback)(void*), void* context, size_t stack_size)
{
    if (!worker.compat || worker.pthread)
        return false;

    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes))
        return false;
    const int stack_result = pthread_attr_setstacksize(&attributes, stack_size);
    int result = stack_result;
    if (!stack_result)
    {
        worker.run = callback;
        worker.context = context;
        worker.handle.store(0, std::memory_order_relaxed);
        // Create through the launcher provider so the renderer keeps its
        // inherited read-only storage scope and is owned by this lifecycle.
        result = worker.create(&worker.pthread, &attributes, Entry, nullptr);
    }
    pthread_attr_destroy(&attributes);
    if ((result == 0) != bool(worker.pthread))
        Retain(Phase::CreateUnknown);
    if (!result)
        ++worker.generation;
    return result == 0;
}

struct CpuSample
{
    uint64_t generation = 0;
    uint64_t ticks = 0;
    bool valid = false;
};

static CpuSample SampleCpu()
{
    // Start, sampling and native close run on MTGS. This handle cannot be
    // closed by a different thread while the SDK call is in progress.
    const auto handle = worker.handle.load(std::memory_order_acquire);
    if (!handle || !worker.pthread)
        return {};
    uint64_t ticks = 0;
    const Result result = svcGetInfo(&ticks, InfoType_ThreadTickCount, handle, TickCountInfo_Total);
    return {worker.generation, ticks, result == 0};
}

static void Join()
{
    Progress.store(Phase::WaitingForHandle, std::memory_order_release);
    const uint64_t ticks = GetTickFrequency();
    const uint64_t start = GetCPUTicks();
    const uint64_t budget = ticks * (ExitTimeoutNs / 1'000'000'000);
    Handle handle = worker.handle.load(std::memory_order_acquire);
    while (!handle && ticks && GetCPUTicks() - start < budget)
    {
        worker.compat->sleep_ns(1'000'000);
        handle = worker.handle.load(std::memory_order_acquire);
    }
    if (!handle || !worker.pthread)
        Retain(Phase::HandleMissing);
    auto* const native = NativeDescriptor(worker.pthread, handle);
    if (!native)
        Retain(Phase::HandleMismatch);

    Progress.store(Phase::WaitingForExit, std::memory_order_release);
    const Result wait = svcWaitSynchronizationSingle(handle, ExitTimeoutNs);
    if (wait || GSBackClose::Worker.load(std::memory_order_acquire) != GSBackClose::WorkerPhase::Returned)
        Retain(Phase::ExitUnconfirmed);
    Progress.store(Phase::Closing, std::memory_order_release);
    if (worker.close(native))
        Retain(Phase::CloseRejected);

    // Match the libounce pthread provider: it allocated this descriptor,
    // while threadClose released the now-dead thread's kernel resources.
    worker.release_descriptor(native);
    worker.pthread = {};
    worker.handle.store(0, std::memory_order_relaxed);
    Progress.store(Phase::Released, std::memory_order_release);
}
}
