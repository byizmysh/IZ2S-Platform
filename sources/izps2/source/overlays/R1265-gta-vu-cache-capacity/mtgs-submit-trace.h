#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

enum class MtgsSubmitPhase : unsigned {
	ReserveRing, CopyRegisters, ReserveVsync, Publish, PublishWake,
	ArmWait, Wake, Wait, Count
};

class MtgsSubmitTrace
{
public:
	static constexpr unsigned SampleLimit = 192;
	static constexpr unsigned Capacity = 12;
	static constexpr unsigned PhaseCount = static_cast<unsigned>(MtgsSubmitPhase::Count);
	struct Sample
	{
		void Mark(MtgsSubmitPhase phase, uint64_t now)
		{
			if (now >= last)
			{
				spans[static_cast<unsigned>(phase)] += now - last;
				last = now;
			}
		}
		uint64_t start = 0, last = 0, cpu = 0;
		uint32_t frame = 0;
		std::array<uint64_t, PhaseCount> spans{};
		bool waited = false;
	};

	void Reset(bool enable, uint64_t threshold)
	{
		*this = {};
		m_enabled = enable;
		m_threshold = threshold;
	}

	bool Active() const { return m_enabled && m_observed < SampleLimit; }

	Sample& Begin(uint32_t frame, uint64_t now, uint64_t cpu)
	{
		m_current = {};
		m_current.start = m_current.last = now;
		m_current.cpu = cpu;
		m_current.frame = frame;
		return m_current;
	}

	void Finish(uint64_t cpu, bool waited)
	{
		++m_observed;
		m_current.cpu = m_current.cpu && cpu >= m_current.cpu ? cpu - m_current.cpu : 0;
		m_current.waited = waited;
		const auto wall = m_current.last - m_current.start;
		if (wall < m_threshold)
			return;
		++m_qualifying;
		if (m_retained < Capacity)
		{
			m_rows[m_retained++] = m_current;
			return;
		}
		auto entry = std::min_element(m_rows.begin(), m_rows.end(), [](const auto& a, const auto& b) {
			return a.last - a.start < b.last - b.start;
		});
		if (wall > entry->last - entry->start)
			*entry = m_current;
	}

	unsigned Observed() const { return m_observed; }
	unsigned Qualifying() const { return m_qualifying; }
	unsigned Retained() const { return m_retained; }
	const Sample& Get(unsigned index) const { return m_rows[index]; }

private:
	std::array<Sample, Capacity> m_rows{};
	Sample m_current{};
	uint64_t m_threshold = 0;
	unsigned m_observed = 0, m_qualifying = 0, m_retained = 0;
	bool m_enabled = false;
};
