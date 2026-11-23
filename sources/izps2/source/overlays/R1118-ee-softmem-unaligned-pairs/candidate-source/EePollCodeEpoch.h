#pragma once

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

    void watchMapping(std::uint32_t page) { m_pages.set(page); }

    void remap(std::uint32_t page, std::uintptr_t old_entry, std::uintptr_t new_entry)
    {
        if (old_entry != new_entry && m_pages[page])
            changed();
    }

    void reset()
    {
        changed();
        m_pages.reset();
    }

    void changed()
    {
        if (m_value != Exhausted)
            ++m_value;
    }

    static constexpr std::uint64_t Exhausted = std::numeric_limits<std::uint64_t>::max();

private:
    static constexpr std::uint32_t VirtualPageCount = 1u << 20;
    std::bitset<VirtualPageCount> m_pages;
    std::uint64_t m_value = 0;
};

const std::uint64_t* eeWatchPollCode(const void* code, std::uint32_t bytes, std::uint32_t first, std::uint32_t last);
