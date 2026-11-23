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
};

alignas(64) WakeSelector s_selector;
std::atomic<bool> s_pool_wake{false};
std::atomic<bool> s_profile{false};
// MTGS owns these counters until worker shutdown. No file writes in the loop.
Window s_current;
Window s_windows[WindowLimit];
size_t s_window_count = 0;

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
    s_pool_wake.store(pool_wake, std::memory_order_relaxed);
    s_profile.store(profile, std::memory_order_relaxed);
}

void EndField(uint32_t field)
{
    if (!field || !s_profile.load(std::memory_order_relaxed) || s_window_count == WindowLimit)
        return;
    if (s_current.fields != 0 && field != s_current.start + s_current.fields)
        s_current = {};
    if (s_current.fields == 0)
        s_current.start = field;
    if (++s_current.fields == FieldsPerWindow)
    {
        s_windows[s_window_count++] = s_current;
        s_current = {};
    }
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
    }
    return report;
}
}
