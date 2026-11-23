#pragma once

#include <cstdint>

// The EE alone writes and reads this byte, including generated code.
// A completed ring receipt is acquired before observing the interrupt word.
struct Ps2MtvuPollGate {
    std::uint8_t quiescent = 0;

    void BeforeSubmit() { quiescent = 0; }
    void Observe(bool empty, std::uint32_t interrupts) { quiescent = empty && interrupts == 0; }
};
static_assert(sizeof(Ps2MtvuPollGate) == 1);

namespace NativePoll {
extern Ps2MtvuPollGate mtvuQuiescence;
void ConfigureMtvuQuietPoll(bool enabled);
void RefreshMtvuQuiescence();
}
