// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "common/Threading.h"

#include <atomic>

// One EE waiter and one GS producer. The producer publishes the retired bytes
// before Checkpoint. Both actors exchange the same flag, which orders the
// producer's publication with the EE thread's second check of the predicate.
class GifRetirementWait
{
public:
    template <typename Ready>
    void Wait(const Ready& ready)
    {
        if (ready())
            return;

        m_armed.exchange(true, std::memory_order_acq_rel);
        if (ready() && m_armed.exchange(false, std::memory_order_acq_rel))
            return;

        // If the consumer took the arm, it owes exactly one signal, even if
        // the bytes became visible before the second predicate check.
        m_ready.Wait();
    }

    void RetirePacket()
    {
        // Request check on each ordinary packet. The periodic exchange and
        // the idle checkpoint cover a request racing the relaxed read.
        if ((++m_packets & 15u) == 0 || m_armed.load(std::memory_order_relaxed))
            Checkpoint();
    }

    void Checkpoint()
    {
        if (m_armed.exchange(false, std::memory_order_acq_rel))
            m_ready.Post();
    }

private:
    std::atomic<bool> m_armed{false};
    Threading::UserspaceSemaphore m_ready;
    unsigned m_packets = 0;
};
