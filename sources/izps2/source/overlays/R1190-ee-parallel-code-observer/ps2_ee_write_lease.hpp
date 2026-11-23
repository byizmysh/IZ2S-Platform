#pragma once

#include <atomic>
#include <cstdint>

// A revocation belongs to this CPU cache lifetime. A new session may
// grant the lease only while no compiled EE block can run.
class Ps2EeWriteLease
{
public:
    void Grant() { m_exact.store(0, std::memory_order_relaxed); }
    void Revoke() { m_exact.store(1, std::memory_order_release); }
    bool Fast() const { return m_exact.load(std::memory_order_acquire) == 0; }
    const std::atomic<std::uint32_t>* Word() const { return &m_exact; }

private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t));
    std::atomic<std::uint32_t> m_exact{1};
};

bool eeActivateParallelObservedCode();
const std::atomic<std::uint32_t>* eeObservedCodeLease();
