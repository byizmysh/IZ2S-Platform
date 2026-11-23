// SPDX-License-Identifier: GPL-3.0+

#include "spu2-certified-mixer.h"
#include "certified-window.h"
#include "common/Console.h"
#include "common/FPControl.h"
#include "common/HostSys.h"
#include "common/Threading.h"

#include <condition_variable>
#include <mutex>
#include <optional>

MULTI_ISA_DEF(void spu2MixCertified();)

namespace Spu2CertifiedMixer
{
namespace
{
// The VM owns the schedule. The worker owns the mixer state only until the
// queue is drained. Every synchronous SPU2 entry point drains it first.
using namespace Spu2Horizon;
constexpr unsigned SubmitSamples = 8;

#ifdef IZPS2_CERTIFIED_MIXER_VERIFY
thread_local bool s_on_worker;
#endif

class Mixer
{
public:
	bool Start()
	{
		m_thread.SetStackSize(256 * 1024);
		return m_thread.Start([this] { Run(); });
	}

	void Advance(u64 now)
	{
		if (now < lClocks || (m_pending_clock && now < m_pending_clock))
		{
			Drain();
			return RunSynchronously(now);
		}

		const auto fp = FPControlRegister::GetCurrent();
		if (m_certified_end && fp != m_window_fp)
			Drain();

		const u64 samples = (now - lClocks) / ClockPerSample;
		if (!samples)
		{
			// Before a window has been certified, fractional input ticks
			// still update in-flight DMA on the IOP owner.
			if (!m_certified_end)
				RunSynchronously(now);
			return;
		}

		const u64 through = lClocks + samples * ClockPerSample;
		if (through > m_certified_end)
		{
			Drain();
			const u64 needed = (now - lClocks) / ClockPerSample;
			if (!needed || needed > Batch)
				return RunSynchronously(now);

			unsigned limit = Batch;
			for (; limit >= needed && Check(limit) != Gate::Allowed; limit >>= 1)
			{
			}
			if (limit < needed)
				return RunSynchronously(now);

			m_window_fp = fp;
			m_certified_end = lClocks + limit * ClockPerSample;
			++m_windows;
		}

		m_pending_clock = through;
		if (through - lClocks >= SubmitSamples * ClockPerSample || through == m_certified_end)
			SubmitPending();
	}

	void Drain()
	{
		if (!m_certified_end)
			return;
		SubmitPending();
		{
			std::unique_lock lock(m_mutex);
			if (m_queued || m_working)
			{
				const u64 begin = GetCPUTicks();
				m_finished.wait(lock, [this] { return m_queued == 0 && m_working == 0; });
				m_wait_ticks += GetCPUTicks() - begin;
				++m_waits;
			}
		}
		m_certified_end = 0;
	}

	void Stop()
	{
		Drain();
		{
			std::lock_guard lock(m_mutex);
			m_stopping = true;
		}
		m_available.notify_one();
		m_thread.Join();
	}

	void Report() const
	{
		const u64 cpu_hz = Threading::GetThreadTicksPerSecond();
		const u64 wall_hz = GetTickFrequency();
		Console.WriteLn("IZPS2_SPU2_CERTIFIED samples=%llu windows=%llu submissions=%llu jobs=%llu "
			"sync_calls=%llu waits=%llu wait_ms=%.3f worker_cpu_ms=%.3f max_outstanding=%u",
			static_cast<unsigned long long>(m_rendered), static_cast<unsigned long long>(m_windows),
			static_cast<unsigned long long>(m_submissions), static_cast<unsigned long long>(m_jobs),
			static_cast<unsigned long long>(m_sync_calls), static_cast<unsigned long long>(m_waits),
			wall_hz ? 1000.0 * m_wait_ticks / wall_hz : 0.0,
			cpu_hz && m_cpu_ticks ? 1000.0 * m_cpu_ticks / cpu_hz : 0.0, m_max_outstanding);
	}

private:
	void RunSynchronously(u64 now)
	{
		++m_sync_calls;
		TimeUpdate(now);
	}

	void SubmitPending()
	{
		if (!m_pending_clock)
			return;

		const unsigned samples = static_cast<unsigned>((m_pending_clock - lClocks) / ClockPerSample);
		lClocks = m_pending_clock;
		m_pending_clock = 0;
		{
			std::lock_guard lock(m_mutex);
			pxAssertRel(samples && m_queued + m_working + samples <= Batch, "SPU2 certified batch exceeded");
			m_queued += samples;
			m_max_outstanding = std::max(m_max_outstanding, m_queued + m_working);
			m_queued_fp = m_window_fp;
		}
		++m_submissions;
		m_available.notify_one();
	}

	void Run()
	{
		Threading::SetNameOfCurrentThread("IZPS2 SPU2 mixer");
		const u64 begin = Threading::GetThreadCpuTime();
#ifdef IZPS2_CERTIFIED_MIXER_VERIFY
		s_on_worker = true;
#endif
		for (;;)
		{
			unsigned samples;
			FPControlRegister fp;
			{
				std::unique_lock lock(m_mutex);
				m_available.wait(lock, [this] { return m_queued || m_stopping; });
				if (m_stopping)
					break;
				samples = m_working = m_queued;
				m_queued = 0;
				fp = m_queued_fp;
			}

			FPControlRegister::SetCurrent(fp);
			for (unsigned i = 0; i < samples; ++i)
			{
				++Cycles;
				for (unsigned core = 0; core < 2; ++core)
				{
					if (Cores[core].KeyOff)
					{
						StopVoices(core, Cores[core].KeyOff);
						Cores[core].KeyOff = 0;
					}
				}
				// Check admitted this window before the owner gave up access.
				MULTI_ISA_SELECT(spu2MixCertified)();
			}

			{
				std::lock_guard lock(m_mutex);
				m_working = 0;
			}
			++m_jobs;
			m_rendered += samples;
			m_finished.notify_one();
		}
		const u64 end = Threading::GetThreadCpuTime();
		if (begin && end >= begin)
			m_cpu_ticks = end - begin;
	}

	std::mutex m_mutex;
	std::condition_variable m_available, m_finished;
	Threading::Thread m_thread;
	bool m_stopping = false;
	unsigned m_queued = 0, m_working = 0, m_max_outstanding = 0;
	FPControlRegister m_window_fp{}, m_queued_fp{};
	u64 m_pending_clock = 0, m_certified_end = 0;
	u64 m_windows = 0, m_waits = 0, m_wait_ticks = 0, m_submissions = 0, m_sync_calls = 0;
	u64 m_jobs = 0, m_rendered = 0, m_cpu_ticks = 0;
};

std::optional<Mixer> s_mixer;
}

bool Start()
{
	if (s_mixer)
		return true;
	if (s_mixer.emplace().Start())
		return true;
	s_mixer.reset();
	return false;
}

void Stop()
{
	if (s_mixer)
	{
		s_mixer->Stop();
		s_mixer->Report();
		s_mixer.reset();
	}
}

static void Drain()
{
	if (s_mixer)
		s_mixer->Drain();
}

static void Advance(u64 now)
{
	if (s_mixer)
		s_mixer->Advance(now);
	else
		TimeUpdate(now);
}

#ifdef IZPS2_CERTIFIED_MIXER_VERIFY
bool IsWorker()
{
	return s_on_worker;
}
#endif
}
