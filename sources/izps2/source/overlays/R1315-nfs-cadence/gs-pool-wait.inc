// SPDX-License-Identifier: GPL-3.0+
#include "gs-pool-wait.h"

#include <fmt/format.h>

namespace GsPoolWait
{
namespace
{
constexpr uint32_t FieldsPerWindow = 120;
constexpr size_t WindowLimit = 256;

struct WaitStats
{
    uint64_t ticks = 0;
    uint64_t max_ticks = 0;
    uint64_t calls = 0;

    void Add(uint64_t elapsed)
    {
        ticks += elapsed;
        max_ticks = std::max(max_ticks, elapsed);
        ++calls;
    }
};

struct Window
{
    uint32_t start = 0;
    uint32_t fields = 0;
    WaitStats draw;
    WaitStats payload;
    uint64_t parent_cpu = 0;
    uint64_t back_cpu = 0;
    bool parent_cpu_valid = false;
    bool back_cpu_valid = false;
};

alignas(64) WakeSelector s_selector;
std::atomic<bool> s_pool_wake{false};
std::atomic<bool> s_profile{false};
// MTGS owns these counters until worker shutdown. No file writes in the loop.
Window s_current;
Window s_windows[WindowLimit];
size_t s_window_count = 0;
uint64_t s_parent_cpu = 0;
GsBackNative::CpuSample s_back_cpu;

struct Transition
{
    uint32_t field = 0, mtvu_packets = 0;
    uint64_t mtvu_bytes = 0;
    uint64_t pmode = 0, smode1 = 0, smode2 = 0, dispfb1 = 0, dispfb2 = 0, csr = 0;
};
constexpr size_t HistoryFields = 8;
constexpr size_t TriggerFields = 24;
Transition s_recent[HistoryFields + TriggerFields];
size_t s_history = 0;
size_t s_after_trigger = 0;

void SampleBoundary()
{
    const uint64_t parent = Threading::GetThreadCpuTime();
    const auto back = GsBackNative::SampleCpu();
    if (s_parent_cpu && parent >= s_parent_cpu)
    {
        s_current.parent_cpu_valid = true;
        s_current.parent_cpu = parent - s_parent_cpu;
    }
    if (s_back_cpu.valid && back.valid && s_back_cpu.generation == back.generation && back.ticks >= s_back_cpu.ticks)
    {
        s_current.back_cpu_valid = true;
        s_current.back_cpu = back.ticks - s_back_cpu.ticks;
    }
    s_parent_cpu = parent;
    s_back_cpu = back;
}

template <typename Ready>
void Wait(GSBackQueue::Channel& channel, Pool pool, Ready&& ready)
{
    const bool targeted = s_pool_wake.load(std::memory_order_relaxed);
    const bool profile = s_profile.load(std::memory_order_relaxed);
    const uint64_t begin = profile ? GetCPUTicks() : 0;
    if (targeted)
        s_selector.Set(pool);
    channel.space.Wait(std::forward<Ready>(ready));
    if (targeted)
        s_selector.Set(Pool::None);
    if (profile)
        (pool == Pool::Draw ? s_current.draw : s_current.payload).Add(GetCPUTicks() - begin);
}
}

void Configure(bool pool_wake, bool profile)
{
    s_selector.Set(Pool::None);
    s_current = {};
    s_window_count = 0;
    s_history = 0;
    s_after_trigger = 0;
    s_pool_wake.store(pool_wake, std::memory_order_relaxed);
    s_profile.store(profile, std::memory_order_relaxed);
    s_parent_cpu = 0;
    s_back_cpu = {};
    if (profile)
        SampleBoundary();
}

void EndField(uint32_t field)
{
    if (!field || !s_profile.load(std::memory_order_relaxed) || s_window_count == WindowLimit)
        return;
    if (s_current.fields != 0 && field != s_current.start + s_current.fields)
    {
        s_current = {};
        s_parent_cpu = 0;
        s_back_cpu = {};
    }
    if (s_current.fields == 0)
        s_current.start = field;
    if (++s_current.fields == FieldsPerWindow)
    {
        SampleBoundary();
        s_windows[s_window_count++] = s_current;
        s_current = {};
    }
}

void NoteTransition(uint32_t field, uint32_t mtvu_packets, uint64_t mtvu_bytes, const unsigned char* registers)
{
    if (!field || s_after_trigger == TriggerFields)
        return;
    const auto reg = [registers](size_t offset) {
        uint64_t value;
        std::memcpy(&value, registers + offset, sizeof(value));
        return value;
    };
    const Transition entry{field, mtvu_packets, mtvu_bytes, reg(0x0000), reg(0x0010), reg(0x0020),
        reg(0x0070), reg(0x0090), reg(0x1000)};
    if (s_after_trigger || mtvu_packets >= 2000)
        s_recent[HistoryFields + s_after_trigger++] = entry;
    else
        s_recent[s_history++ % HistoryFields] = entry;
}

std::string Report()
{
    if (!s_profile.load(std::memory_order_relaxed))
        return {};
    std::string report = fmt::format("IZPS2_GS_POOL_WAKE targeted={} windows={} wall_hz={}\n",
        unsigned(s_pool_wake.load(std::memory_order_relaxed)), s_window_count, GetTickFrequency());
    for (size_t i = 0; i < s_window_count; ++i)
    {
        const Window& w = s_windows[i];
        report += fmt::format("IZPS2_GS_POOL_WINDOW start={} fields={} draw_calls={} draw_ticks={} draw_max={} payload_calls={} payload_ticks={} payload_max={}\n",
            w.start, w.fields, w.draw.calls, w.draw.ticks, w.draw.max_ticks,
            w.payload.calls, w.payload.ticks, w.payload.max_ticks);
        report += fmt::format("IZPS2_GS_WORKER_WINDOW start={} fields={} cpu_hz={} mtgs_valid={} mtgs_ticks={} back_valid={} back_ticks={}\n",
            w.start, w.fields, Threading::GetThreadTicksPerSecond(), unsigned(w.parent_cpu_valid),
            w.parent_cpu, unsigned(w.back_cpu_valid), w.back_cpu);
    }
    if (s_after_trigger)
    {
        const auto append = [&](const Transition& w) {
            report += fmt::format("IZPS2_GS_TRANSITION field={} mtvu_packets={} mtvu_bytes={} pmode={:016x} smode1={:016x} smode2={:016x} dispfb1={:016x} dispfb2={:016x} csr={:016x}\n",
                w.field, w.mtvu_packets, w.mtvu_bytes, w.pmode, w.smode1, w.smode2, w.dispfb1, w.dispfb2, w.csr);
        };
        const size_t count = std::min(s_history, HistoryFields);
        for (size_t i = s_history - count; i < s_history; ++i)
            append(s_recent[i % HistoryFields]);
        for (size_t i = 0; i < s_after_trigger; ++i)
            append(s_recent[HistoryFields + i]);
    }
    return report;
}
}
