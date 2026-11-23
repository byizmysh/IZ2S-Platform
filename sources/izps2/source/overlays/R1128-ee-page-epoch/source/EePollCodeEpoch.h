#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <limits>

// EE-owner stamp for reusing a frozen poll-loop snapshot. Old guards fall
// back to an exact comparison after any observed write to compiled code.
class EePollCodeEpoch
{
public:
    const std::uint64_t* watch() const
    {
        return m_value == Exhausted ? nullptr : &m_value;
    }

    const std::uint32_t* watchPage(std::uint32_t physical)
    {
        auto& epoch = m_singlePages[physical];
        if (epoch == PageEpochExhausted)
            return nullptr;
        m_singleWatched.set(physical);
        return &epoch;
    }

    void changedPage(std::uint32_t physical)
    {
        changed();
        invalidatePage(physical);
    }

    void invalidateAll()
    {
        changed();
        for (std::uint32_t page = 0; page < RamPageCount; ++page)
            invalidatePage(page);
    }

    void watchMapping(std::uint32_t page) { m_pages.set(page); }

    void remap(std::uint32_t page, std::uintptr_t old_entry, std::uintptr_t new_entry)
    {
        if (old_entry == new_entry)
            return;
        if (m_pages[page])
            changed();
        // The resident-loop emitter admits only the canonical RAM,
        // KSEG0 and KSEG1 aliases. Invalidate their shared physical stamp.
        const auto alias = page >> 17;
        const auto physical = page & ((1u << 17) - 1);
        if ((alias == 0 || alias == 4 || alias == 5) && physical < RamPageCount)
            invalidatePage(physical);
    }

    void reset()
    {
        invalidateAll();
        m_pages.reset();
        m_singleWatched.reset();
    }

    void changed()
    {
        if (m_value != Exhausted)
            ++m_value;
    }

    static constexpr std::uint64_t Exhausted = std::numeric_limits<std::uint64_t>::max();

private:
    static constexpr std::uint32_t RamPageCount = (128u * 1024 * 1024) >> 12;
    static constexpr std::uint32_t PageEpochExhausted = std::numeric_limits<std::uint32_t>::max();

    void invalidatePage(std::uint32_t page)
    {
        if (!m_singleWatched[page])
            return;
        auto& epoch = m_singlePages[page];
        if (epoch != PageEpochExhausted)
            ++epoch;
    }

    std::array<std::uint32_t, RamPageCount> m_singlePages{};
    std::bitset<RamPageCount> m_singleWatched;
    static constexpr std::uint32_t VirtualPageCount = 1u << 20;
    std::bitset<VirtualPageCount> m_pages;
    std::uint64_t m_value = 0;
};

const std::uint64_t* eeWatchPollCode(const void* code, std::uint32_t bytes, std::uint32_t first, std::uint32_t last);

const std::uint32_t* eeWatchResidentCodePage(const void* code, std::uint32_t bytes, std::uint32_t physical);
