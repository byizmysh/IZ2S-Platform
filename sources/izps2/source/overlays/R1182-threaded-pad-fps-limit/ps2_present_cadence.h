#pragma once

#include <cstdint>

class Ps2PresentCadence {
public:
    void Configure(std::uint64_t ticks_per_second, unsigned rate)
    {
        period = rate ? (ticks_per_second + rate / 2) / rate : 0;
        Reset();
    }

    bool Enabled() const { return period != 0; }
    bool Due(std::uint64_t now) const { return !next || now >= next; }
    void Reset() { next = 0; }

    // Keep the phase when scanout and content rates do not divide evenly.
    // Start a new period after a long delay so there is no catch-up burst.
    void Accepted(std::uint64_t now)
    {
        if (period)
            next = (next && now >= next && now - next < period ? next : now) + period;
    }

private:
    std::uint64_t period = 0;
    std::uint64_t next = 0;
};
