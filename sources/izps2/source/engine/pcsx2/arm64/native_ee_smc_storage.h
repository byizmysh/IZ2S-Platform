#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// EE-owned immutable data for the lifetime of one JIT generation.
class NativeEeSmcStorage
{
public:
    const std::uint8_t* Append(const std::uint8_t* bytes, std::size_t count)
    {
        const std::size_t aligned = (m_used + 7) & ~std::size_t{7};
        if (count > sizeof(m_data) - aligned)
            return nullptr;
        std::memcpy(m_data + aligned, bytes, count);
        m_used = aligned + count;
        return m_data + aligned;
    }

    void Reset() { m_used = 0; }

private:
    alignas(8) std::uint8_t m_data[2048 * 1024];
    std::size_t m_used;
};
