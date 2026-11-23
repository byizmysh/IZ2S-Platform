// SPDX-License-Identifier: GPL-3.0+

#include "spu2-certified-mixer.h"
#include "certified-window.h"
#include "common/Console.h"
#include "common/FPControl.h"
#include "common/HostSys.h"
#include "common/Threading.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
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
constexpr unsigned RenewAhead = Batch / 2;

enum class WaitCause : unsigned { Client, Horizon, Clock, FloatingPoint, Space, Count };
struct WaitStats
{
	u64 count = 0;
	u64 ticks = 0;
	u64 max_ticks = 0;
};

#ifdef IZPS2_CERTIFIED_MIXER_VERIFY
thread_local bool s_on_worker;
#endif

class Mixer
{
public:
	explicit Mixer(bool worker_renewal) : m_worker_renewal(worker_renewal) {}

	bool Start()
	{
		m_thread.SetStackSize(256 * 1024);
		return m_thread.Start([this] { Run(); });
	}

	void Advance(u64 now)
	{
		if (now < lClocks || (m_pending_clock && now < m_pending_clock))
		{
			Drain(WaitCause::Clock);
			return RunSynchronously(now);
		}

		const auto fp = FPControlRegister::GetCurrent();
		u64 certified_end = m_certified_end.load(std::memory_order_acquire);
		if (certified_end && fp != m_window_fp)
		{
			Drain(WaitCause::FloatingPoint);
			certified_end = 0;
		}

		const u64 samples = (now - lClocks) / ClockPerSample;
		if (!samples)
		{
			// With DMA idle throughout the active certificate, fractional
			// ticks do not require a separate TimeUpdate on the producer.
			if (!certified_end)
				RunSynchronously(now);
			return;
		}

		const u64 through = lClocks + samples * ClockPerSample;
		if (samples > Batch || through > certified_end)
		{
			// First let the worker finish any partially submitted old window.
			// If it certifies the next range, its state stays on the worker.
			if (samples > Batch || !WaitForRenewal(through, certified_end))
			{
				Drain(WaitCause::Horizon);
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
				certified_end = lClocks + limit * ClockPerSample;
				m_certified_end.store(certified_end, std::memory_order_release);
				++m_windows;
			}
		}

		m_pending_clock = through;
		if (through - lClocks >= SubmitSamples * ClockPerSample || through == certified_end)
			SubmitPending();
	}

	void Drain(WaitCause cause = WaitCause::Client)
	{
		if (!m_certified_end.load(std::memory_order_acquire))
			return;
		SubmitPending();
		std::unique_lock lock(m_mutex);
		Wait(lock, cause, [this] { return Idle(); });
		m_certified_end.store(0, std::memory_order_release);
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
		Console.WriteLn("IZPS2_SPU2_WORKER_RENEWAL enabled=%u attempts=%llu grants=%llu handoffs_saved=%llu",
			unsigned(m_worker_renewal), static_cast<unsigned long long>(m_renew_attempts),
			static_cast<unsigned long long>(m_worker_grants), static_cast<unsigned long long>(m_handoffs_saved));
		constexpr std::array names{"client", "horizon", "clock", "fp", "space"};
		static_assert(names.size() == static_cast<unsigned>(WaitCause::Count));
		for (unsigned i = 0; i < names.size(); ++i)
		{
			const auto& stats = m_wait_stats[i];
			Console.WriteLn("IZPS2_SPU2_WORKER_WAIT cause=%s count=%llu ms=%.3f max_ms=%.3f",
				names[i], static_cast<unsigned long long>(stats.count),
				wall_hz ? 1000.0 * stats.ticks / wall_hz : 0.0,
				wall_hz ? 1000.0 * stats.max_ticks / wall_hz : 0.0);
		}
	}

private:
	bool Idle() const { return m_queued == 0 && m_working == 0; }

	template <typename Predicate>
	void Wait(std::unique_lock<std::mutex>& lock, WaitCause cause, const Predicate& ready)
	{
		if (ready())
			return;
		const u64 begin = GetCPUTicks();
		m_finished.wait(lock, ready);
		const u64 elapsed = GetCPUTicks() - begin;
		auto& stats = m_wait_stats[static_cast<unsigned>(cause)];
		++stats.count;
		stats.ticks += elapsed;
		stats.max_ticks = std::max(stats.max_ticks, elapsed);
		++m_waits;
		m_wait_ticks += elapsed;
	}

	bool WaitForRenewal(u64 through, u64& certified_end)
	{
		if (!m_worker_renewal || !certified_end)
			return false;

		SubmitPending();
		std::unique_lock lock(m_mutex);
		Wait(lock, WaitCause::Horizon, [this, through] {
			return Idle() || m_certified_end.load(std::memory_order_acquire) >= through;
		});
		certified_end = m_certified_end.load(std::memory_order_acquire);
		if (certified_end < through)
			return false;
		++m_handoffs_saved;
		return true;
	}

	void RenewFromWorker(u64 rendered_through)
	{
		const u64 previous = m_certified_end.load(std::memory_order_acquire);
		if (rendered_through > previous || previous - rendered_through > RenewAhead * ClockPerSample)
			return;

		// The worker has just retired this exact prefix and still owns the
		// decoder, effect cursors and IRQ state. Renew only with a fresh
		// conservative check. Earlier grants remain valid on rejection.
		++m_renew_attempts;
		for (unsigned limit = Batch; rendered_through + limit * ClockPerSample > previous; limit >>= 1)
		{
			if (Check(limit) != Gate::Allowed)
				continue;
			m_certified_end.store(rendered_through + limit * ClockPerSample, std::memory_order_release);
			++m_worker_grants;
			break;
		}
	}

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
		pxAssertRel(samples && samples <= Batch, "SPU2 certified batch exceeded");
		{
			std::unique_lock lock(m_mutex);
			Wait(lock, WaitCause::Space, [this, samples] { return m_queued + m_working + samples <= Batch; });
			lClocks = m_queued_through = m_pending_clock;
			m_pending_clock = 0;
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
			u64 rendered_through;
			FPControlRegister fp;
			{
				std::unique_lock lock(m_mutex);
				m_available.wait(lock, [this] { return m_queued || m_stopping; });
				if (m_stopping)
					break;
				samples = m_working = m_queued;
				m_queued = 0;
				rendered_through = m_queued_through;
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

			if (m_worker_renewal)
				RenewFromWorker(rendered_through);

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
	const bool m_worker_renewal;
	bool m_stopping = false;
	unsigned m_queued = 0, m_working = 0, m_max_outstanding = 0;
	FPControlRegister m_window_fp{}, m_queued_fp{};
	std::atomic<u64> m_certified_end{0};
	u64 m_pending_clock = 0, m_queued_through = 0;
	std::array<WaitStats, static_cast<unsigned>(WaitCause::Count)> m_wait_stats{};
	u64 m_renew_attempts = 0, m_worker_grants = 0, m_handoffs_saved = 0;
	u64 m_windows = 0, m_waits = 0, m_wait_ticks = 0, m_submissions = 0, m_sync_calls = 0;
	u64 m_jobs = 0, m_rendered = 0, m_cpu_ticks = 0;
};

std::optional<Mixer> s_mixer;
}

bool Start()
{
#ifdef IZPS2_CERTIFIED_MIXER_VERIFY
	return Start(std::getenv("IZ_LOCAL_SPU2_WORKER_RENEW") != nullptr);
#else
	return Start(true);
#endif
}

bool Start(bool worker_renewal)
{
	if (s_mixer)
		return true;
	if (s_mixer.emplace(worker_renewal).Start())
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
