#pragma once

#include "native_ee_warm_profile.hpp"

#include <unordered_set>
#include <memory>
#include <new>
#include <vector>

namespace ps2::ee_warm {

class LateWarmCapture
{
public:
    static constexpr unsigned BurstCount = 8;
    static constexpr unsigned EntriesPerBurst = 768;
    static constexpr unsigned Limit = 2048;
    static constexpr uint32_t FrameThresholdMs = 40;
    static constexpr uint32_t CompileThresholdMs = 20;

    LateWarmCapture(uint64_t frame_threshold, uint64_t compile_threshold)
        : m_coverage(new (std::nothrow) uint64_t[CoverageWords]{}), m_frame_threshold(frame_threshold),
          m_compile_threshold(compile_threshold) {}

    bool Available() const { return bool(m_coverage); }

    // Physical spans are observed even when they cannot be warmed. A later
    // entry inside an existing block would move an event check to a new PC.
    void Cover(uint32_t physical, uint32_t bytes)
    {
        if (Available() && ValidSpan(physical, bytes))
            MarkSpan(physical, bytes);
    }

    void Observe(uint32_t physical, uint32_t pc, uint32_t bytes, bool eligible, uint64_t hash)
    {
        if (!Available() || !ValidSpan(physical, bytes))
            return;
        const bool overlap = MarkSpan(physical, bytes);
        if (!overlap && eligible && m_armed && m_pending.count < m_pending.records.size())
            m_pending.records[m_pending.count++] = {pc, bytes, hash, m_order++};
    }

    void OnField(uint32_t field, uint64_t tick, uint64_t compile_ticks)
    {
        if (m_armed && field == m_frame + 1 && tick >= m_tick && compile_ticks >= m_compile_ticks &&
            tick - m_tick >= m_frame_threshold && m_pending.count)
        {
            const auto cost = compile_ticks - m_compile_ticks;
            if (cost >= m_compile_threshold)
            {
                // Put the most expensive complete intervals at the front of the next warmup.
                auto weakest = std::min_element(m_bursts.begin(), m_bursts.end(),
                    [](const Burst& a, const Burst& b) { return a.cost < b.cost; });
                if (cost > weakest->cost)
                {
                    m_pending.cost = cost;
                    m_pending.frame = m_frame;
                    *weakest = m_pending;
                }
            }
        }

        m_pending.count = 0;
        m_frame = field;
        m_tick = tick;
        m_compile_ticks = compile_ticks;
        m_armed = true;
    }

    void Discontinuity()
    {
        m_armed = false;
        m_pending.count = 0;
    }

    // previous has already passed the framed-file and warm profile checks.
    // Keep older useful entries at the end so this session can replace them.
    bool Build(uint32_t disc, uint32_t elf, std::span<const uint8_t> previous,
               std::vector<uint8_t>& output) const
    {
        std::array<const Burst*, BurstCount> priority{};
        for (size_t i = 0; i < priority.size(); ++i)
            priority[i] = &m_bursts[i];
        std::sort(priority.begin(), priority.end(), [](const Burst* a, const Burst* b) {
            if (a->cost != b->cost)
                return a->cost > b->cost;
            return a->frame < b->frame;
        });
        if (!priority.front()->cost)
            return false;

        output.assign(HeaderBytes, 0);
        output.reserve(HeaderBytes + RecordBytes * Limit);
        std::copy(Magic.begin(), Magic.end(), output.begin());
        WriteLE(std::span(output).subspan(8, 4), disc);
        WriteLE(std::span(output).subspan(12, 4), elf);
        std::unordered_set<uint32_t> seen;
        seen.reserve(Limit);
        const auto append = [&](WarmEntry record) {
            if (seen.size() == Limit || !seen.insert(record.pc).second)
                return;
            const auto start = output.size();
            output.resize(start + RecordBytes);
            WriteLE(std::span(output).subspan(start, 4), record.pc);
            WriteLE(std::span(output).subspan(start + 4, 4), record.bytes);
            WriteLE(std::span(output).subspan(start + 8, 8), record.hash);
        };
        // The previous accepted list ran before guest execution this time.
        // Reserve at least half the file for newly observed bursts.
        constexpr size_t PreviousLimit = HeaderBytes + RecordBytes * (Limit / 2);
        for (size_t start = HeaderBytes; start + RecordBytes <= std::min(previous.size(), PreviousLimit);
             start += RecordBytes)
        {
            append({static_cast<uint32_t>(ReadLE(previous.subspan(start, 4))),
                    static_cast<uint32_t>(ReadLE(previous.subspan(start + 4, 4))),
                    ReadLE(previous.subspan(start + 8, 8)), 0});
        }
        // Fill the available capacity in cost order, then restore the
        // guest's compilation order before writing the selected entries.
        std::vector<WarmEntry> ordered;
        const size_t capacity = Limit - seen.size();
        ordered.reserve(capacity);
        for (const auto* burst : priority)
        {
            const auto count = std::min(burst->count, capacity - ordered.size());
            ordered.insert(ordered.end(), burst->records.begin(), burst->records.begin() + count);
        }
        std::sort(ordered.begin(), ordered.end(), [](const WarmEntry& a, const WarmEntry& b) {
            return a.order < b.order;
        });
        for (const auto& entry : ordered)
            append(entry);
        WriteLE(std::span(output).subspan(16, 4), seen.size());
        return !std::equal(output.begin(), output.end(), previous.begin(), previous.end());
    }

private:
    static constexpr uint32_t RamBytes = 32u << 20;
    static constexpr size_t CoverageWords = RamBytes / 4 / 64;

    static bool ValidSpan(uint32_t address, uint32_t bytes)
    {
        return !(address % 4) && !(bytes % 4) && bytes && address < RamBytes && bytes <= RamBytes - address;
    }

    bool MarkSpan(uint32_t address, uint32_t bytes)
    {
        size_t first = address / 4;
        const size_t end = first + bytes / 4;
        bool overlap = false;
        while (first < end)
        {
            const unsigned lane = first % 64;
            const auto count = std::min<size_t>(64 - lane, end - first);
            const uint64_t mask = (~uint64_t{0} >> (64 - count)) << lane;
            auto& coverage = m_coverage[first / 64];
            overlap |= bool(coverage & mask);
            coverage |= mask;
            first += count;
        }
        return overlap;
    }

    struct WarmEntry
    {
        uint32_t pc, bytes;
        uint64_t hash, order;
    };
    struct Burst
    {
        std::array<WarmEntry, EntriesPerBurst> records{};
        size_t count = 0;
        uint64_t cost = 0;
        uint32_t frame = 0;
    };

    std::unique_ptr<uint64_t[]> m_coverage;
    std::array<Burst, BurstCount> m_bursts{};
    Burst m_pending;
    uint64_t m_frame_threshold, m_compile_threshold;
    uint64_t m_tick = 0, m_compile_ticks = 0;
    uint32_t m_frame = 0;
    uint64_t m_order = 0;
    bool m_armed = false;
};

extern LateWarmCapture* active_late_warm_capture;

} // namespace ps2::ee_warm
