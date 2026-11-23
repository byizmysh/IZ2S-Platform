#include "ps2_timing_windows.hpp"

#include <cassert>
#include <iostream>

namespace
{
using Windows = Ps2TimingWindows<uint32_t>;

void AddFields(Windows& windows, uint32_t first, uint32_t last)
{
    for (uint32_t field = first; field <= last; ++field)
        windows.Add(field, 200 + field * 10, [field] { return field; });
}

void Check(const Windows::Window& window, uint32_t first)
{
    assert(window.first_frame == first && window.first == first);
    assert(window.last == first + Windows::Intervals);
    assert(window.elapsed == 10 * Windows::Intervals);
    assert((window.Percentiles() == std::array<uint64_t, 3>{10, 10, 10}));
}
}

int main()
{
    Windows windows;
    windows.Start(true);
    constexpr uint32_t completed = 159;
    AddFields(windows, 0, completed * Windows::Intervals);
    assert(windows.Recording() && windows.Count() == Windows::Capacity);
    for (size_t i = 0; i < windows.Count(); ++i)
    {
        const uint32_t sequence = i < Windows::KeepStartup ? i : completed - Windows::Capacity + i;
        Check(windows.Get(i), sequence * Windows::Intervals);
    }

    windows.Pause();
    const uint32_t resume = completed * Windows::Intervals + 10;
    AddFields(windows, resume, resume + Windows::Intervals + 14);
    Check(windows.Get(Windows::Capacity - 1), resume);
    windows.Stop();
    assert(!windows.Recording());

    windows.Start();
    AddFields(windows, 0, completed * Windows::Intervals);
    assert(!windows.Recording() && windows.Count() == Windows::Capacity);
    for (size_t i = 0; i < windows.Count(); ++i)
        Check(windows.Get(i), i * Windows::Intervals);

    std::cout << "{\"initialRetained\":12,\"latestRetained\":36,\"windowsSimulated\":159,"
                 "\"partialWindowIgnored\":true,\"stockBehaviorPreserved\":true}\n";
}
