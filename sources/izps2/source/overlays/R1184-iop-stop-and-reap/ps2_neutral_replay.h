#pragma once

#include <cstdint>

struct Ps2NeutralReplay {
    static constexpr std::uint32_t Intervals = 2160;
    enum class State { Inactive, Running, Complete, Interrupted, Discontinuity };

    State state = State::Inactive;
    std::uint32_t first = 0, last = 0, observations = 0;
    std::uint64_t first_tick = 0, last_tick = 0;

    void Observe(std::uint32_t frame, std::uint64_t tick) {
        if (state != State::Running)
            return;
        if (!observations) {
            first = frame;
            first_tick = tick;
        } else if (frame != last + 1 || tick < last_tick) {
            state = State::Discontinuity;
            return;
        }
        last = frame;
        last_tick = tick;
        if (++observations == Intervals + 1)
            state = State::Complete;
    }

    void Interrupt() {
        if (state == State::Running)
            state = State::Interrupted;
    }
};
