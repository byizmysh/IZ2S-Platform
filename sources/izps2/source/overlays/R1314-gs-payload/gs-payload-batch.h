// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace GsPayloadBatch
{
constexpr uint32_t Capacity = 4 * 1024 * 1024;
constexpr uint32_t LargestPacket = 256 * 1024;
constexpr uint32_t Alignment = 32;
constexpr uint32_t TrailingPadding = 32;

// MTGS is the sole writer. The release callback publishes a FIFO record after
// every transfer that points into the retired slab. The backend returns it to
// the existing pool only when that record reaches the front of the queue.
template <typename Node>
class Slab
{
public:
    template <typename Acquire, typename Release>
    unsigned char* Reserve(uint32_t size, Acquire&& acquire, Release&& release)
    {
        if (size == 0 || size > LargestPacket)
            return nullptr;
        const uint32_t aligned_size = ((size + Alignment - 1) & ~(Alignment - 1)) + TrailingPadding;
        if (!m_node || aligned_size > Capacity - m_used)
        {
            if (m_node)
                release(m_node);
            m_node = acquire();
            m_used = 0;
        }
        unsigned char* const data = m_node->buff + m_used;
        m_used += aligned_size;
        return data;
    }

    // Only the owning channel frees its arena. Drop a borrowed cursor when
    // the front parser goes away, after its destructor has drained the FIFO.
    void Clear() { m_node = nullptr; m_used = 0; }

private:
    Node* m_node = nullptr;
    uint32_t m_used = 0;
};

void Configure(bool enabled);
std::string Report();
}
