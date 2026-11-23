#pragma once

#include <cstdint>

struct Ps2NeutralReplay {
    static constexpr std::uint32_t Intervals = 2160;
    enum class State { Inactive, Running, Complete, Interrupted, Discontinuity };

    State state = State::Inactive;
    std::uint32_t first = 0, last = 0, observations = 0;
    std::uint64_t first_tick = 0, last_tick = 0;
    std::uint32_t warmup_intervals = 0, measured_intervals = Intervals;
    std::uint32_t warmup_remaining = 0;
    std::uint32_t sequence_frame = 0;
    std::uint64_t sequence_tick = 0;

    void Start(std::uint32_t warmup, std::uint32_t measured) {
        *this = {};
        state = State::Running;
        warmup_intervals = warmup_remaining = warmup;
        measured_intervals = measured;
    }

    void Observe(std::uint32_t frame, std::uint64_t tick) {
        if (state != State::Running)
            return;
        if ((observations || warmup_remaining != warmup_intervals) &&
            (frame != sequence_frame + 1 || tick < sequence_tick)) {
            state = State::Discontinuity;
            return;
        }
        sequence_frame = frame;
        sequence_tick = tick;
        if (warmup_remaining) {
            --warmup_remaining;
            return;
        }
        if (!observations) {
            first = frame;
            first_tick = tick;
        }
        last = frame;
        last_tick = tick;
        if (++observations == measured_intervals + 1)
            state = State::Complete;
    }

    void Interrupt() {
        if (state == State::Running)
            state = State::Interrupted;
    }
};
