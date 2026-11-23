#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace EeGap
{
enum class Phase : unsigned
{
    Owner,
    Guest,
    Event,
    Iop,
    Counters,
    Dma,
    Vu,
    VsyncTasks,
    Limiter,
    GsSubmit,
    Input,
    VblankMarker,
    VblankSampling,
    Count
};

inline constexpr unsigned PhaseCount = static_cast<unsigned>(Phase::Count);
inline constexpr const char* PhaseNames[] = {
    "owner", "guest", "event", "iop", "counters", "dma", "vu",
    "vsync_tasks", "limiter", "gs_submit", "input", "vblank_sd_marker",
    "vblank_sampling"
};
static_assert(std::size(PhaseNames) == PhaseCount);

// The EE owner alone accesses the trace, including all nested event hooks.
class Trace
{
public:
    static constexpr unsigned FrameLimit = 192;
    static constexpr unsigned Capacity = 12;

    struct Span
    {
        std::uint64_t total = 0;
        std::uint64_t max_segment = 0;
    };

    struct Frame
    {
        std::uint64_t elapsed = 0;
        std::uint32_t start_frame = 0;
        std::array<Span, PhaseCount> spans{};
    };

    void Start(std::uint64_t threshold)
    {
        *this = {};
        m_threshold = threshold;
    }

    unsigned Enter(Phase phase, std::uint64_t now)
    {
        if (m_depth == m_stack.size())
            return 0;
        Advance(now);
        m_stack[m_depth++] = phase;
        return m_depth;
    }

    void Leave(unsigned depth, std::uint64_t now)
    {
        if (!depth || depth > m_depth)
            return;
        Advance(now);
        // Execute may leave its generated-code scopes by a nonlocal exit.
        m_depth = depth - 1;
    }

    bool OnVblank(std::uint32_t frame, std::uint64_t now)
    {
        if (m_have_frame && frame == m_frame + 1 && now >= m_tick)
        {
            Advance(now);
            const auto elapsed = now - m_frame_tick;
            if (elapsed >= m_threshold)
            {
                ++m_qualified;
                Record({elapsed, m_frame, m_contents});
            }
            ++m_intervals;
        }
        m_contents = {};
        m_frame = frame;
        m_tick = m_frame_tick = now;
        m_have_frame = true;
        return m_intervals < FrameLimit;
    }

    void Discontinuity() { m_have_frame = false; }
    unsigned Count() const { return m_count; }
    unsigned Qualified() const { return m_qualified; }
    unsigned Intervals() const { return m_intervals; }
    const Frame& Get(unsigned index) const { return m_peaks[index]; }

private:
    void Advance(std::uint64_t now)
    {
        if (!m_have_frame || now < m_tick)
            return;
        const auto phase = m_depth ? m_stack[m_depth - 1] : Phase::Owner;
        auto& span = m_contents[static_cast<unsigned>(phase)];
        const auto elapsed = now - m_tick;
        span.total += elapsed;
        if (span.max_segment < elapsed)
            span.max_segment = elapsed;
        m_tick = now;
    }

    void Record(const Frame& frame)
    {
        if (m_count < m_peaks.size())
        {
            m_peaks[m_count++] = frame;
            return;
        }
        auto* lowest = &m_peaks[0];
        for (auto& peak : m_peaks)
        {
            if (peak.elapsed < lowest->elapsed)
                lowest = &peak;
        }
        if (frame.elapsed > lowest->elapsed)
            *lowest = frame;
    }

    std::array<Frame, Capacity> m_peaks{};
    std::array<Span, PhaseCount> m_contents{};
    std::array<Phase, 16> m_stack{};
    std::uint64_t m_threshold = 0, m_tick = 0, m_frame_tick = 0;
    std::uint32_t m_frame = 0;
    unsigned m_intervals = 0, m_depth = 0, m_count = 0, m_qualified = 0;
    bool m_have_frame = false;
};

extern Trace* active;
std::uint64_t Tick();

class Scope
{
public:
    explicit Scope(Phase phase) : m_trace(active)
    {
        if (m_trace)
            m_depth = m_trace->Enter(phase, Tick());
    }

    void Close()
    {
        if (m_trace && m_trace == active)
            m_trace->Leave(m_depth, Tick());
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Trace* m_trace;
    unsigned m_depth = 0;
};

// The recompiler can nonlocally unwind this call. All locals in these
// hooks are trivially destructible. The owning Execute scope reconciles
// the stack when it returns from the recompiler.
template<class Call>
inline void Measure(Phase phase, Call call)
{
    Scope scope(phase);
    call();
    scope.Close();
}
}
