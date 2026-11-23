// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <atomic>
#include <cstdint>

namespace NfsPodium
{

class Trigger
{
public:
    static constexpr unsigned ConfirmedFields = 8;

    void Arm(bool enabled)
    {
        *this = {};
        m_armed = enabled;
    }

    bool Armed() const { return m_armed; }

    bool Observe(std::uint32_t field, std::uint32_t packets, std::uint64_t bytes)
    {
        if (!m_armed)
            return false;
        const bool podium = (packets == 3451 && bytes == 5854368) ||
            (packets == 3461 && bytes == 5855088) ||
            (packets == 3463 && bytes == 5855232);
        if (field != m_field + 1 || !podium)
            m_consecutive = 0;
        m_field = field;
        if (!podium || ++m_consecutive != ConfirmedFields)
            return false;
        m_armed = false;
        return true;
    }

private:
    std::uint32_t m_field = 0;
    unsigned m_consecutive = 0;
    bool m_armed = false;
};

inline std::atomic<std::uint32_t> ReadyField{0}; // MTGS publishes, EE consumes.
inline thread_local Trigger Detector;           // MTGS only.

}
