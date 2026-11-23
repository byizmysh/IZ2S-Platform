#pragma once

#include <atomic>
#include <cstdint>

// The EE is the sole producer and the sole waiter. Reserve before making a
// command visible in the ring. The GS consumes a reservation only after VSync.
class MtgsVsyncQueue
{
public:
	void Reserve()
	{
		m_state.fetch_add(1, std::memory_order_relaxed);
	}

	[[nodiscard]] int Pending() const
	{
		return static_cast<int>(m_state.load(std::memory_order_acquire) & CountMask);
	}

	// Making the limit check and registration one operation prevents a
	// completed frame from passing between them. A successful registration
	// transfers responsibility for exactly one semaphore credit to the GS.
	[[nodiscard]] bool ArmIfOverLimit(int limit)
	{
		const auto maximum = static_cast<std::uint32_t>(limit > 0 ? limit : 0);
		auto state = m_state.load(std::memory_order_acquire);
		while ((state & CountMask) > maximum)
		{
			if (m_state.compare_exchange_weak(state, state | Waiting,
					std::memory_order_acq_rel, std::memory_order_acquire))
				return true;
		}
		return false;
	}

	[[nodiscard]] bool Complete()
	{
		auto state = m_state.load(std::memory_order_relaxed);
		for (;;)
		{
			const auto pending = state & CountMask;
			if (!pending)
				return false;
			if (m_state.compare_exchange_weak(state, pending - 1,
					std::memory_order_acq_rel, std::memory_order_relaxed))
				return (state & Waiting) != 0;
		}
	}

	// The ring is discarded only at reset or when the GS worker is closing.
	[[nodiscard]] bool Clear()
	{
		return (m_state.exchange(0, std::memory_order_acq_rel) & Waiting) != 0;
	}

private:
	static constexpr std::uint32_t Waiting = std::uint32_t{1} << 31;
	static constexpr std::uint32_t CountMask = Waiting - 1;
	std::atomic<std::uint32_t> m_state{0};
};
