// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace GsPoolWait
{
enum class Pool : uint8_t { None, Draw, Payload };

// The producer advertises the resource it is waiting for before SpaceWait
// arms its semaphore. SpaceWait still owns the arm/recheck/timeout protocol.
class WakeSelector
{
public:
    void Set(Pool pool) { m_pool.store(pool, std::memory_order_relaxed); }

    template <typename Channel>
    unsigned GuardDepth(const Channel& channel) const
    {
        switch (m_pool.load(std::memory_order_relaxed))
        {
        case Pool::Draw:
            if (channel.draw_free.Size() >= Channel::kDrawRefill)
                return 0;
            break;
        case Pool::Payload:
            if (channel.payload_free.Size() >= Channel::kPayloadRefill)
                return 0;
            break;
        case Pool::None:
            break;
        }
        // A ready pool is equivalent to an exhausted queue for the starvation
        // guard. Otherwise it keeps the original low-water behavior.
        return channel.ring.Size();
    }

private:
    std::atomic<Pool> m_pool{Pool::None};
};

// Configure on MTGS before the first guest packet, report after closing MTGS.
void Configure(bool pool_wake, bool profile);
void EndField(uint32_t field);
std::string Report();
}
