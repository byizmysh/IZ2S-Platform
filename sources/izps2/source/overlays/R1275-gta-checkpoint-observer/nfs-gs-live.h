// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <atomic>
#include <cstdint>

namespace NfsGsLive
{
struct ActiveSpan
{
    uint64_t encoded = 0;
    static constexpr uint64_t TickMask = (uint64_t{1} << 56) - 1;

    bool Active() const { return encoded != 0; }
    bool Elapsed(uint64_t now, uint64_t threshold) const
    {
        const uint64_t age = Age(now);
        return Active() && age >= threshold && age <= TickMask / 2;
    }
    uint64_t Age(uint64_t now) const { return Active() ? (now - Tick()) & TickMask : 0; }
    uint64_t Tick() const { return encoded & TickMask; }
    unsigned Kind() const { return Active() ? unsigned(encoded >> 56) - 1 : 0; }
};

class alignas(64) Slot
{
public:
    void Begin(unsigned kind, uint64_t tick)
    {
        // Publish both fields in one atomic store for a sampler on another core.
        m_span.store((uint64_t(kind + 1) << 56) | (tick & ActiveSpan::TickMask),
            std::memory_order_release);
    }
    void End() { m_span.store(0, std::memory_order_release); }
    ActiveSpan Read() const { return {m_span.load(std::memory_order_acquire)}; }

private:
    std::atomic<uint64_t> m_span{0};
};

inline Slot EeWait;
inline Slot GsCommand;
inline std::atomic<bool> Enabled{false};

inline std::atomic<unsigned> GsPhase{0};
inline thread_local bool TracePhases = false;
inline thread_local unsigned CurrentPhase = 0;

class ScopedPhase
{
public:
    explicit ScopedPhase(unsigned phase) : m_previous(CurrentPhase), m_active(TracePhases)
    {
        if (m_active)
            SetPhase(phase);
    }

    ~ScopedPhase()
    {
        if (m_active)
            SetPhase(m_previous);
    }
    ScopedPhase(const ScopedPhase&) = delete;
    ScopedPhase& operator=(const ScopedPhase&) = delete;

private:
    static void SetPhase(unsigned phase)
    {
        CurrentPhase = phase;
        GsPhase.store(phase, std::memory_order_relaxed);
    }

    unsigned m_previous;
    bool m_active;
};
}
