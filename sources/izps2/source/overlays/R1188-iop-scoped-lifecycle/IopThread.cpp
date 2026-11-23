#include "native_iop_contract.h"
#ifndef PS2_NATIVE_IOP_SINGLE_PUBLISH_FENCE
#define PS2_NATIVE_IOP_SINGLE_PUBLISH_FENCE 0
#endif
#ifndef PS2_NATIVE_IOP_HOLD_TARGET_FASTPATH
#define PS2_NATIVE_IOP_HOLD_TARGET_FASTPATH 0
#endif
#if (PS2_NATIVE_IOP_SINGLE_PUBLISH_FENCE || PS2_NATIVE_IOP_HOLD_TARGET_FASTPATH) && !PS2_NATIVE_IOP_THREADED
#error native IOP synchronization opt-ins require native threaded IOP
#endif
#if PS2_NATIVE_IOP_THREADED
#include "mtvu_domains_contract_v1.h"
#include <cstdlib>
#endif
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// The IOP thread object. The states, the sleep handshake and the deadlock argument are in IopThread.h.

#include "IopThread/IopThread.h"
#include "ps2_iop_jit_mailbox.hpp"
#include "ps2_iop_jit_owner.hpp"

#include "PerformanceMetrics.h"
#include "common/Assertions.h"
#include "common/FPControl.h"
#include "common/HostSys.h"

#include <algorithm>
#include <chrono>
#include <limits>

#if defined(__linux__) && !PS2_NATIVE_IOP_THREADED
#include <linux/membarrier.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace IopThread
{
	namespace
	{
		// How long a Lifecycle hold, and the IOP waiting for room in the I2E queue, sleep between
		// looks once they have given up spinning.
		constexpr auto kPollSleep = std::chrono::microseconds(20);
		constexpr auto kI2ESpacePollSleep = std::chrono::microseconds(50);

		// Spins in the I2E-space wait before it starts sleeping. The EE empties the queue at every
		// event test, a few microseconds apart, so this almost always ends it.
		constexpr u32 kI2ESpaceSpins = 4096;

		// A Lifecycle hold that is sleeping wakes a parked IOP every this many looks.
		constexpr u64 kSleepingWakeInterval = 64;

		struct EeSpinHint
		{
#if PS2_NATIVE_IOP_THREADED && defined(__aarch64__) && !defined(_MSC_VER)
			const bool use_isb = g_spin_hint.load(std::memory_order_relaxed) == SpinHintKind::Isb;

			__fi void Pause() const
			{
				if (use_isb)
					asm volatile("isb" ::: "memory");
				else
					asm volatile("yield" ::: "memory");
			}
#else
			__fi void Pause() const { SpinHint(); }
#endif
		};

		// Keep idle deadline comparisons in the clock's native unit. The native
		// clock uses the same platform tick source as HBXL's monotonic clock.
		struct IopClock
		{
#if PS2_NATIVE_IOP_THREADED
			static constexpr u64 NsPerSecond = 1'000'000'000;

			static u64 Now() { return GetCPUTicks(); }

			static u64 Budget(u64 ns)
			{
				// Round up so a non-integral deadline never expires early.
				return (ns * GetTickFrequency() + NsPerSecond - 1) / NsPerSecond;
			}

			static u64 ToNanoseconds(u64 ticks, u64 hz)
			{
				if (ticks <= std::numeric_limits<u64>::max() / NsPerSecond)
					return ticks * NsPerSecond / hz;
				return (ticks / hz) * NsPerSecond + (ticks % hz) * NsPerSecond / hz;
			}

			static u64 ElapsedNs(u64 begin)
			{
				const u64 elapsed = Now() - begin;
				return ToNanoseconds(elapsed, GetTickFrequency());
			}
#else
			static u64 Now()
			{
				return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			}

			static u64 Budget(u64 ns) { return ns; }
			static u64 ElapsedNs(u64 begin) { return Now() - begin; }
#endif
		};

#if defined(__linux__) && !PS2_NATIVE_IOP_THREADED
		long Membarrier(int cmd)
		{
			return syscall(__NR_membarrier, cmd, 0, 0);
		}

		// MEMBARRIER_CMD_PRIVATE_EXPEDITED needs the process registered first (Linux 4.14). Registration is
		// process-wide and cannot be undone, so it is done once.
		bool RegisterMembarrier()
		{
			static const bool s_ok = [] {
				const long cmds = Membarrier(MEMBARRIER_CMD_QUERY);
				if (cmds < 0 || !(cmds & MEMBARRIER_CMD_PRIVATE_EXPEDITED))
					return false;
				return Membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED) == 0;
			}();
			return s_ok;
		}
#endif

		// Waits until another core writes the cache line holding `word` (or `word` is already not
		// `expected`), the event stream fires, or an interrupt comes. Like ShortSpinOn (HostSys.cpp),
		// for a 64-bit word: LDAXR arms the exclusive monitor on the line, and a store to it by the EE
		// clears the monitor, which is the event WFE waits for. Elsewhere it is the spin hint.
		__fi void WaitForLineChange(const std::atomic<u64>& word, u64 expected)
		{
#if defined(__aarch64__) && !defined(_MSC_VER)
			u64 seen;
			__asm__ __volatile__(
				"sevl\n"
				"wfe\n"
				"ldaxr %0, [%1]\n"
				"cmp   %0, %2\n"
				"b.ne  1f\n"
				"wfe\n"
				"1:\n"
				: "=&r"(seen)
				: "r"(&word), "r"(expected)
				: "cc", "memory");
			(void)seen;
#else
			(void)word;
			(void)expected;
			SpinHint();
#endif
		}
	} // namespace

	Thread::Thread() = default;

	Thread::~Thread()
	{
		Stop();
	}

	bool Thread::Start(Params params)
	{
		if (m_running)
			return false;
		if (params.mode == IopThreadMode::Off || !params.run_slice)
			return false;
		pxAssertMsg(params.lag > 0, "the lag bound must be positive");

		m_params = std::move(params);
		m_mode = m_params.mode;
		m_lag = m_params.lag;
		m_min_slice = std::max<s32>(m_params.min_slice, 0);
#if defined(__linux__) && !PS2_NATIVE_IOP_THREADED
		m_use_membarrier = (m_params.sleep_barrier == SleepBarrier::Auto) && RegisterMembarrier();
#else
		m_use_membarrier = false;
#endif
		m_ee_fence = !m_use_membarrier;

		// Nothing else touches any of this: the IOP thread does not exist yet. The thread's creation
		// orders these writes before its first read.
		m_e2i.Reset();
		m_i2e.Reset();
		m_e2i_kicks.TakeAll();
		m_call.fn = nullptr;
		m_call.arg = nullptr;
		m_call.state.store(kCallIdle, std::memory_order_relaxed);

		m_iop_pos = m_params.start_iop_pos;
		m_iop.pos_pub.store(m_iop_pos, std::memory_order_relaxed);
		m_iop.idle.store(false, std::memory_order_relaxed);
		m_sleep.sleeping.store(false, std::memory_order_relaxed);
		m_iop.held_gen.store(0, std::memory_order_relaxed);
		m_iop.done_seq.store(0, std::memory_order_relaxed);
		m_ee_wm_local = m_params.start_ee_cycle;
		m_ee.wm.store(m_ee_wm_local, std::memory_order_relaxed);
		m_ee.hold_target.store(kNoHold, std::memory_order_relaxed);
		m_ee.hold_gen.store(0, std::memory_order_relaxed);
		m_ee.spinning.store(false, std::memory_order_relaxed);
		m_ee.shutdown.store(false, std::memory_order_relaxed);
		m_ee.req_seq.store(0, std::memory_order_relaxed);
		m_last_seen_iop_pos = m_iop_pos;
		m_hold_requested = false;
		m_hold_pending = false;
		m_hold_gen_local = 0;
		m_in_call = false;
		m_req_seq_local = 0;
		m_iop_tid = std::thread::id();

		for (detail::Counter* c : {&m_lag_spins, &m_lag_spin_iterations, &m_lag_stall_ns, &m_lag_spins_parked, &m_lag_parked_stall_ns,
				 &m_emu_hold_requests, &m_emu_hold_stall_ns, &m_sif_ee_pumps, &m_hold_requests, &m_hold_markers, &m_hold_spin_iterations, &m_hold_sleeps,
				 &m_e2i_full_spins, &m_e2i_full_iterations, &m_e2i_full_failfast, &m_i2e_drained, &m_calls_served, &m_wakes_sent,
				 &m_wakes_dropped, &m_stop_checks, &m_handoffs, &m_slices, &m_items_applied, &m_parks, &m_holds_entered,
				 &m_calls_posted, &m_calls_cancelled, &m_i2e_full_waits, &m_kicks_applied, &m_lag_spins_idle, &m_lag_idle_stall_ns,
				 &m_idle_waits, &m_idle_ns})
			c->Reset();

		// A hold that is live when the thread starts keeps it stopped: the IOP comes up held at the
		// position it was given, so there is nothing to catch up and it touches nothing.
		if (m_mode == IopThreadMode::Threaded && m_hold_depth > 0)
		{
			m_hold_gen_local = 1;
			m_ee.hold_gen.store(1, std::memory_order_relaxed);
			m_ee.hold_target.store(m_iop_pos, std::memory_order_relaxed);
			m_hold_requested = true;
		}

		m_sema.Reset();
		m_started.store(false, std::memory_order_relaxed);
		if (m_params.stack_size != 0)
			m_thread.SetStackSize(m_params.stack_size);
		// Open after the semaphore is reset: a host thread whose post is accepted wakes the semaphore, and
		// that wake-up must not be reset away.
#if PS2_NATIVE_IOP_THREADED
        if(ps2_native_iop_startup_result()) {m_mode=IopThreadMode::Off;return false;}
        // Keep external posts closed until native ownership is accepted.
#else
        m_host.Open();
#endif
		// Only IdleWait reads this private slot. Keep its ABI and store the
		// configured budget in the clock's unit for this worker lifetime.
		m_idle_spin_ns = m_mode == IopThreadMode::Threaded && m_params.idle_spin_ns != 0
			? IopClock::Budget(m_params.idle_spin_ns) : 0;
        const bool hosted_iop_owner = iz_iop_jit_selects_thread(*this);
        if (hosted_iop_owner && (m_mode != IopThreadMode::Threaded || !iz_iop_jit_attach(*this)))
        {
            m_host.Close();
            m_mode = IopThreadMode::Off;
            return false;
        }
        if (hosted_iop_owner)
            iz_iop_jit_arm_scoped_create();
        const bool thread_started = m_thread.Start([this] { ThreadMain(); });
        if (hosted_iop_owner)
            iz_iop_jit_disarm_scoped_create();
        if (!thread_started)
		{
			m_host.Close();
            if (hosted_iop_owner)
                iz_iop_jit_detach(*this);
#if PS2_NATIVE_IOP_THREADED
            ps2_native_iop_start_failed();
            m_mode=IopThreadMode::Off;
#endif
            return false;
		}

		// The thread has finished its setup (and recorded its id) when it sets this. A caller can
		// rely on the FPCR, the name and OnIopThread() as soon as Start() returns.
        while (!m_started.load(std::memory_order_acquire))
            std::this_thread::yield();
#if PS2_NATIVE_IOP_THREADED
        if(ps2_native_iop_startup_result()) {
            m_host.Close();
            ps2_native_iop_failed_start_join_prepare((uint64_t)(uintptr_t)(void*)GetThreadHandle());
            m_thread.Join();
            // This cancels PREPARED only after the actual matching successful
            // pthread_join observer. A failed/unobserved join retains state.
            ps2_native_iop_start_failed();
            m_ee.shutdown.store(true,std::memory_order_release);
            m_iop_tid=std::thread::id();m_mode=IopThreadMode::Off;
            m_hold_requested=false;m_hold_pending=false;
            return false;
        }
        m_host.Open();
#endif
        if (hosted_iop_owner && !iz_iop_jit_view().rx)
        {
            m_host.Close();
            iz_iop_jit_prepare_worker_reap(static_cast<void*>(GetThreadHandle()));
            m_thread.Join();
            iz_iop_jit_reap_after_join();
            iz_iop_jit_detach(*this);
            m_iop_tid = std::thread::id();
            m_mode = IopThreadMode::Off;
            m_hold_requested = false;
            m_hold_pending = false;
            return false;
        }
        m_running = true;
		m_start_count++;
		return true;
	}

	void Thread::Stop()
	{
		if (!m_running)
			return;
		pxAssertMsg(!OnIopThread(), "the IOP thread cannot stop itself");
		pxAssertMsg(!m_in_call, "Stop() from inside a CallOnEE function");

		// Host threads stop being accepted first. What was accepted before this point is in the queue, and
		// the IOP thread runs it before it exits (ThreadMain), so a command is never left behind.
		m_host.Close();

		// Every wait the IOP has checks this flag, so it never waits for the EE past here: a
		// CallOnEE still Posted is cancelled, a full I2E queue gives up, a hold ends.
		m_ee.shutdown.store(true, std::memory_order_release);
		m_hold_requested = false; // a ScopedHold destroyed after Stop() has nothing to release
		if (!iz_iop_jit_notify(*this))
			m_sema.NotifyOfWork(); // not filtered: this one must not be lost
        const bool hosted_iop_owner = iz_iop_jit_selects_thread(*this);
        if (hosted_iop_owner)
            iz_iop_jit_prepare_worker_reap(static_cast<void*>(GetThreadHandle()));
		m_thread.Join();
		m_running = false;
        if (hosted_iop_owner)
        {
            iz_iop_jit_reap_after_join();
            iz_iop_jit_detach(*this);
        }
		m_iop_tid = std::thread::id();
		m_mode = IopThreadMode::Off;
	}

	void Thread::ThreadMain()
	{
		// Per-thread setup, as in MTVU.cpp and MTGS.cpp.
		Threading::SetNameOfCurrentThread(m_params.name);
		PerformanceMetrics::AdpfRegisterCallingThread();
		PageFaultHandler::InstallSecondaryThread();
		FPControlRegister::SetCurrent(m_params.fpcr);

		m_iop_tid = std::this_thread::get_id();
        const bool hosted_iop_owner = iz_iop_jit_selects_thread(*this);
        if (hosted_iop_owner && !iz_iop_jit_open_on_worker())
        {
            iz_iop_jit_leave_worker();
            m_started.store(true, std::memory_order_release);
            return;
        }
#if PS2_NATIVE_IOP_THREADED
        ps2_native_iop_worker_enter();
        const bool entry_failed=ps2_native_iop_startup_result()!=0;
        m_started.store(true,std::memory_order_release);
        if(entry_failed)return; // no IOP ownership: no loop, callbacks or finished receipt
#else
        m_started.store(true,std::memory_order_release);
#endif

		if (m_mode == IopThreadMode::Lockstep)
			LockstepLoop();
		else
			ThreadedLoop();

		// Stop() closed the queue to host threads before it asked this thread to leave; whatever they got in
		// before that runs here, on the thread that owns the devices, with the CPU thread waiting for the join.
        if (hosted_iop_owner)
            iz_iop_jit_note_worker_drain();
		m_host.RunPending();
        if (hosted_iop_owner)
            iz_iop_jit_leave_worker();
#if PS2_NATIVE_IOP_THREADED
        ps2_native_iop_worker_finished();
#endif
	}

	// Parks until `ready()` reports work or the thread is woken: the hold wait, the CallOnEE wait and
	// Lockstep's wait for a request. `ready` re-reads everything that can end the wait after `sleeping`
	// is set. Those wake-ups are never filtered by `sleeping` (hold release, call completion, Stop()) or,
	// in Lockstep, are ordered by a seq_cst store and load on the EE side, so a seq_cst store here is
	// enough. WorkSema spins for SPIN_TIME_NS before it blocks.
	template <class Ready>
	void Thread::ParkUntil(Ready&& ready)
	{
		m_iop.idle.store(true, std::memory_order_relaxed);
		m_sleep.sleeping.store(true, std::memory_order_seq_cst);
		if (!ready())
		{
			m_parks.Add();
            if (iz_iop_jit_selects_thread(*this))
                iz_iop_jit_park();
            else
                m_sema.WaitForWorkWithSpin();
		}
		m_sleep.sleeping.store(false, std::memory_order_relaxed);
		m_iop.idle.store(false, std::memory_order_relaxed);
	}

	// The IOP's half of the sleep handshake's barrier (IopThread.h, "Going to sleep"). False if the
	// barrier could not be made, in which case the IOP must not sleep.
	bool Thread::IopStoreLoadBarrier()
	{
#if defined(__linux__) && !PS2_NATIVE_IOP_THREADED
		if (m_use_membarrier)
			return Membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED) == 0;
#endif
		std::atomic_thread_fence(std::memory_order_seq_cst);
		return true;
	}

	static __fi bool CheckWorkAvailable(const auto& ee, auto& host, auto& kicks, auto& e2i,
		const auto& has_event, u64 pos, s64 min_slice, u64* watched_wm, u64* future_head = nullptr)
	{
		if (ee.shutdown.load(std::memory_order_seq_cst))
			return true;
		if (ee.hold_target.load(std::memory_order_seq_cst) != kNoHold)
			return true;
		if (host.HasPending() || kicks.Pending())
			return true;
		// The EE pushes stamped items before publishing this watermark. Use the
		// same observation for the queue check and for arming the spin wait.
		const u64 wm = ee.wm.load(std::memory_order_seq_cst);
		if (watched_wm)
			*watched_wm = wm;
		// Only this consumer can advance an observed head. IdleWait does not
		// consume it, and a ring reset requires the worker to be held or stopped.
		const u64 next = future_head && *future_head != UINT64_MAX ? *future_head : e2i.NextStamp();
		if (future_head)
			*future_head = next;
		if (next <= pos)
			return true;
		const s64 budget = static_cast<s64>(std::min(wm, next) - pos);
		if (budget > 0 && (budget >= min_slice || next <= wm || ee.spinning.load(std::memory_order_relaxed)))
			return true;
		return has_event && has_event();
	}

	template <typename Plan>
	static __fi Plan PlanSlice(u64 wm, u64 next, u64 pos, s64 min_slice,
		const std::atomic<bool>& spinning, const auto& has_event)
	{
		const u64 target = std::min(wm, next);
		const s64 budget = static_cast<s64>(target - pos);

		bool run = false;
		if (budget > 0)
		{
			// An item at the end of the slice is `next <= wm`, because target is then next.
			run = budget >= min_slice || next <= wm || spinning.load(std::memory_order_relaxed);
		}
		if (!run && has_event && has_event())
			run = true;
		return {run, target};
	}

	Thread::Plan Thread::MakePlan(u64 wm)
	{
		return PlanSlice<Plan>(wm, m_e2i.NextStamp(), m_iop_pos, m_min_slice, m_ee.spinning, m_params.has_event);
	}

	bool Thread::WorkAvailable()
	{
		return CheckWorkAvailable(m_ee, m_host, m_e2i_kicks, m_e2i, m_params.has_event, m_iop_pos, m_min_slice, nullptr);
	}

	template <size_t Capacity>
	static __fi u64 ApplyDueAndGetStamp(StampedRing<Capacity>& queue, const u64& pos,
		const auto& apply_item, auto& items_applied)
	{
		while (const E2IItem* head = queue.Peek())
		{
			const u64 stamp = head->stamp;
			if (stamp > pos)
				return stamp;
			const E2IItem item = *head;
			queue.Pop();
			if (apply_item)
				apply_item(item);
			items_applied.Add();
		}
		return StampedRing<Capacity>::kNoStamp;
	}

	void Thread::ApplyDueItems()
	{
		ApplyDueAndGetStamp(m_e2i, m_iop_pos, m_params.apply_item, m_items_applied);
	}

	void Thread::ApplyKicks()
	{
		if (!m_e2i_kicks.Pending())
			return;
		const u32 bits = m_e2i_kicks.TakeAll();
		if (bits == 0)
			return;
		m_kicks_applied.Add();
		if (m_params.apply_kicks)
			m_params.apply_kicks(bits);
	}

	// The IOP is the only consumer. A previously observed future head stays
	// at the front while the slice runs, even if the EE appends more items.
	template <size_t Capacity>
	static __fi u64 RunSliceAndPublish(u64 target, u64 next, u64& pos, const auto& params,
		StampedRing<Capacity>& queue, auto& items_applied, std::atomic<u64>& published_pos, auto& slices)
	{
		u64 reached = params.run_slice(pos, target);
		pxAssertMsg(reached >= pos, "run_slice moved the IOP backwards");
		if (reached < pos)
			reached = pos;
		pos = reached;
		if (next == StampedRing<Capacity>::kNoStamp || next <= reached)
			next = ApplyDueAndGetStamp(queue, pos, params.apply_item, items_applied);
		// Every item that fell due is applied before the EE sees the new position.
		published_pos.store(pos, std::memory_order_release);
		slices.Add();
		return next;
	}

	void Thread::RunOneSlice(u64 target)
	{
		RunSliceAndPublish(target, decltype(m_e2i)::kNoStamp, m_iop_pos,
			m_params, m_e2i, m_items_applied, m_iop.pos_pub, m_slices);
	}

	void Thread::ThreadedLoop()
	{
		constexpr u64 no_stamp = decltype(m_e2i)::kNoStamp;
		u64 next = no_stamp;
		// Both the idle spin and its final park check share the current queue head.
		const auto idle_wait = [this](u64& next)
		{
			m_idle_waits.Add();
			m_iop.idle.store(true, std::memory_order_relaxed);
			const u64 budget = m_idle_spin_ns;
	#if PS2_NATIVE_IOP_THREADED
			u64 begin = 0;
			// On the native runtime the clock read enters the kernel. Check
			// work each time, but sample the park deadline every eight polls.
			constexpr u32 kClockPollInterval = 8;
			u64 polls = 0;
	#else
			const u64 begin = IopClock::Now();
	#endif
			for (;;)
			{
				u64 wm;
				if (CheckWorkAvailable(m_ee, m_host, m_e2i_kicks, m_e2i, m_params.has_event,
						m_iop_pos, m_min_slice, &wm, &next))
					break;
	#if PS2_NATIVE_IOP_THREADED
				// Start timing only when the worker is actually about to spin.
				if (budget && polls == 0)
					begin = IopClock::Now();
				if (budget == 0 || ((++polls & (kClockPollInterval - 1)) == 0 && IopClock::Now() - begin >= budget))
	#else
				if (budget == 0 || IopClock::Now() - begin >= budget)
	#endif
				{
					m_sleep.sleeping.store(true, std::memory_order_seq_cst);
					if (IopStoreLoadBarrier())
					{
						if (!CheckWorkAvailable(m_ee, m_host, m_e2i_kicks, m_e2i,
								m_params.has_event, m_iop_pos, m_min_slice, nullptr, &next))
						{
							m_parks.Add();
                            if (iz_iop_jit_selects_thread(*this))
                                iz_iop_jit_park();
                            else
                                m_sema.WaitForWork();
						}
						m_sleep.sleeping.store(false, std::memory_order_relaxed);
						break;
					}
					m_sleep.sleeping.store(false, std::memory_order_relaxed);
				}
				WaitForLineChange(m_ee.wm, wm);
			}
			m_iop.idle.store(false, std::memory_order_relaxed);
	#if !PS2_NATIVE_IOP_THREADED
			m_idle_ns.Add(IopClock::ElapsedNs(begin));
	#endif
		};
		while (!m_ee.shutdown.load(std::memory_order_acquire))
		{
#if PS2_NATIVE_IOP_HOLD_TARGET_FASTPATH
			// A request racing this check is handled at the next loop boundary.
			// The EE still waits for the matching generation before touching IOP state.
			if (m_ee.hold_target.load(std::memory_order_acquire) != kNoHold && TryHold()) [[unlikely]]
#else
			if (TryHold())
#endif
			{
				// The EE can reset the ring while it holds the IOP.
				next = no_stamp;
				continue;
			}
			if (m_host.HasPending())
				m_host.RunPending();
			ApplyKicks();

			// Acquire the watermark before looking at a potentially empty ring.
			// If the last slice left a future head, the EE can only append behind it.
			const u64 wm = m_ee.wm.load(std::memory_order_acquire);
			// The idle checks can observe a due item without consuming it.
			if (next == no_stamp || next <= m_iop_pos)
			{
				next = ApplyDueAndGetStamp(m_e2i, m_iop_pos, m_params.apply_item, m_items_applied);
				// The EE can post between these two observations of an empty ring.
				if (next == no_stamp)
				{
					next = m_e2i.NextStamp();
					// A producer can append an already due item between the reads.
					// Consume it before letting the cached head survive IdleWait.
					if (next <= m_iop_pos)
						next = ApplyDueAndGetStamp(m_e2i, m_iop_pos, m_params.apply_item, m_items_applied);
				}
			}
			const Plan plan = PlanSlice<Plan>(wm, next, m_iop_pos, m_min_slice, m_ee.spinning, m_params.has_event);
			if (plan.run)
			{
				next = RunSliceAndPublish(plan.target, next, m_iop_pos,
					m_params, m_e2i, m_items_applied, m_iop.pos_pub, m_slices);
				continue;
			}

			// Keep the earliest stamp seen by either path. A hold discards it
			// because the EE can reset the ring while the IOP is held.
			idle_wait(next);
		}
	}

	// A hold request, if there is one: run up to its cycle, say so, and stay still until it ends.
	// Returns false if there is no request.
	bool Thread::TryHold()
	{
		// A consistent (target, generation) pair. The EE stores the generation, then the target
		// (release), and clears the target before it bumps the generation again, so a generation
		// that reads the same before and after the target is the one that target was stored with.
		u64 target;
		u32 gen;
		for (;;)
		{
			const u32 before = m_ee.hold_gen.load(std::memory_order_acquire);
			target = m_ee.hold_target.load(std::memory_order_acquire);
			gen = m_ee.hold_gen.load(std::memory_order_relaxed);
			if (gen == before)
				break;
		}
		if (target == kNoHold)
			return false;

		// As in the normal loop, a future head cannot move until this
		// consumer reaches it. Retain it across partial catch-up slices.
		constexpr u64 no_stamp = decltype(m_e2i)::kNoStamp;
		u64 next = no_stamp;
		for (;;)
		{
			if (m_ee.shutdown.load(std::memory_order_acquire))
				return true;
			if (next == no_stamp)
				next = ApplyDueAndGetStamp(m_e2i, m_iop_pos, m_params.apply_item, m_items_applied);
			if (static_cast<s64>(target - m_iop_pos) <= 0)
				break;
			if (next == no_stamp)
				next = m_e2i.NextStamp();
			next = RunSliceAndPublish(std::min(target, next), next, m_iop_pos,
				m_params, m_e2i, m_items_applied, m_iop.pos_pub, m_slices);
		}
		ApplyKicks();

		m_holds_entered.Add();
		// Pairs with the acquire load of held_gen in AcquireHold(): everything this thread wrote is
		// visible to the EE once it sees its generation here. If the EE gave the request up while
		// this ran (its stop predicate fired), or made a newer one, this answers a generation nobody
		// waits for and HoldWait() returns at once, because the generation has moved on.
		m_iop.held_gen.store(gen, std::memory_order_release);
		HoldWait(gen);
		return true;
	}

	void Thread::HoldWait(u32 gen)
	{
		// The hold ends when hold_target is cleared, or when the generation moves on (the EE released
		// it and asked again before this thread looked), or on shutdown.
		const auto still_held = [this, gen] {
			return !m_ee.shutdown.load(std::memory_order_seq_cst) && m_ee.hold_target.load(std::memory_order_seq_cst) != kNoHold &&
			       m_ee.hold_gen.load(std::memory_order_seq_cst) == gen;
		};
		while (still_held())
		{
			Ps2IopJitMailbox::Service(*this);
			ParkUntil([&] { return !still_held() || Ps2IopJitMailbox::Pending(*this); });
		}
	}

	bool Thread::PostI2E(u16 kind, u32 arg)
	{
		if (m_i2e.TryPush(kind, arg)) [[likely]]
			return true;
		return WaitForI2ESpace(kind, arg);
	}

	// The IOP waits for the EE here, which the deadlock argument in IopThread.h allows. The EE empties
	// the queue at every event test and in every spin, so this is short. It polls rather than parks so
	// that the EE's drain has nothing to notify.
	bool Thread::WaitForI2ESpace(u16 kind, u32 arg)
	{
		m_i2e_full_waits.Add();
		for (u32 n = 0;; n++)
		{
			if (m_ee.shutdown.load(std::memory_order_acquire))
				return false;
			if (m_i2e.TryPush(kind, arg))
				return true;
			if (n < kI2ESpaceSpins)
				SpinHint();
			else
				std::this_thread::sleep_for(kI2ESpacePollSleep);
		}
	}

	bool Thread::CallOnEEImpl(void (*fn)(void*), void* arg)
	{
#if PS2_NATIVE_IOP_THREADED
        // Never wait for the other CPU while owning its JIT writer gate.
        if(!ps2_mtvu_code_host_caller_idle())std::abort();
#endif
		if (m_mode == IopThreadMode::Lockstep)
		{
			// The EE is blocked in the handoff and ordered with this thread by it.
			fn(arg);
			return true;
		}
		pxAssertMsg(OnIopThread(), "CallOnEE from a thread that is not the IOP thread");

		m_call.fn = fn;
		m_call.arg = arg;
		// Pairs with the acquire CAS in ServeCall().
		m_call.state.store(kCallPosted, std::memory_order_release);
		m_calls_posted.Add();

		if (!PostI2E(I2EKind::CallOnEE, 0))
		{
			m_call.state.store(kCallIdle, std::memory_order_relaxed);
			m_calls_cancelled.Add();
			return false;
		}

		for (;;)
		{
			u32 state = m_call.state.load(std::memory_order_acquire);
			if (state == kCallDone)
			{
				m_call.state.store(kCallIdle, std::memory_order_relaxed);
				return true;
			}
			if (state == kCallPosted && m_ee.shutdown.load(std::memory_order_acquire))
			{
				// Take the request back before the EE does. If the EE got there first it is running
				// the function; wait for it, because the function may be using this thread's state.
				if (m_call.state.compare_exchange_strong(state, kCallIdle, std::memory_order_acq_rel, std::memory_order_acquire))
				{
					m_calls_cancelled.Add();
					return false;
				}
				continue;
			}
			// The EE callback can patch IOP code while this thread is parked.
			// This route handles only native code publication at that point.
			if (state == kCallRunning)
				Ps2IopJitMailbox::Service(*this);
			ParkUntil([this] {
				const u32 s = m_call.state.load(std::memory_order_seq_cst);
				return s == kCallDone || (s == kCallPosted && m_ee.shutdown.load(std::memory_order_seq_cst)) ||
				       (s == kCallRunning && Ps2IopJitMailbox::Pending(*this));
			});
		}
	}

	bool Thread::ShouldStop()
	{
		m_stop_checks.Add();
		return m_params.should_stop && m_params.should_stop();
	}

	void Thread::WakeIfParked(WakeSite site)
	{
		// The EE half of the sleep handshake: every caller has just stored what the IOP should see.
		EeStoreLoadBarrier();
		if (!m_sleep.sleeping.load(std::memory_order_relaxed))
			return;
		if (m_params.drop_wake && m_params.drop_wake(site))
		{
			m_wakes_dropped.Add();
			return;
		}
		m_wakes_sent.Add();
		if (!iz_iop_jit_notify(*this))
			m_sema.NotifyOfWork();
	}

	void Thread::WakeAfterPublish(u64 cycle)
	{
		// Waking an IOP with no budget to use is a wasted syscall: wake it when the EE is at least
		// S_min (and at least one cycle) ahead of where it was last seen.
		if (static_cast<s64>(cycle - m_last_seen_iop_pos) >= std::max<s64>(m_min_slice, 1))
        {
#if PS2_NATIVE_IOP_SINGLE_PUBLISH_FENCE
            // The sole caller is PublishWatermark, after its release wm store,
            // EE StoreLoad barrier and sleeping load. That barrier already
            // orders the publication before ALL later loads; it does not
            // expire between the two sleeping reads. Keep this second read,
            // the identical test hook, counters and notification. Only the
            // duplicated EeStoreLoadBarrier in WakeIfParked is omitted.
            // The sleeper still publishes sleeping, performs its own full
            // barrier and rechecks all work before it can block/repark.
            if (!m_sleep.sleeping.load(std::memory_order_relaxed))
                return;
            if (m_params.drop_wake && m_params.drop_wake(WakeSite::Publish))
            {
                m_wakes_dropped.Add();
                return;
            }
            m_wakes_sent.Add();
            if (!iz_iop_jit_notify(*this))
            	m_sema.NotifyOfWork();
#else
            WakeIfParked(WakeSite::Publish);
#endif
        }
	}

	void Thread::Wake()
	{
		WakeIfParked(WakeSite::Post);
	}

	void Thread::KickIop(u32 bits)
	{
		// A raise that finds every bit already up is covered by the earlier raise, whose wake-up
		// the IOP has either acted on or will.
		if (m_e2i_kicks.Raise(bits) != 0)
			WakeIfParked(WakeSite::Kick);
	}

	size_t Thread::DrainI2E()
	{
		size_t taken = 0;
		while (const I2EItem* head = m_i2e.Peek())
		{
			// Copy before Pop(): the slot is the IOP's to reuse once it is released. And pop before
			// handling, so a handler that waits and drains again sees only the later items.
			const I2EItem item = *head;
			m_i2e.Pop();
			taken++;
			if (item.kind == I2EKind::CallOnEE)
				ServeCall();
			else if (m_params.handle_i2e)
				m_params.handle_i2e(item);
		}
		m_i2e_drained.Add(taken);
		return taken;
	}

	void Thread::ServeCall()
	{
#if PS2_NATIVE_IOP_THREADED
        // Never wait for the other CPU while owning its JIT writer gate.
        if(!ps2_mtvu_code_host_caller_idle())std::abort();
#endif
		// An entry whose request is gone (cancelled) or already served finds the slot in another
		// state and does nothing. A newer request it finds Posted is served early, which is fine:
		// each request runs once, because only one claim succeeds.
		u32 expected = kCallPosted;
		if (!m_call.state.compare_exchange_strong(expected, kCallRunning, std::memory_order_acq_rel, std::memory_order_acquire))
			return;

		void (*fn)(void*) = m_call.fn;
		void* arg = m_call.arg;
		const bool was_in_call = m_in_call;
		m_in_call = true;
		fn(arg);
		m_in_call = was_in_call;
		m_calls_served.Add();

		// Pairs with the acquire load of the state in CallOnEEImpl(): what the function wrote is
		// visible to the IOP when it sees Done.
		m_call.state.store(kCallDone, std::memory_order_release);
		if (!iz_iop_jit_notify(*this))
			m_sema.NotifyOfWork(); // not filtered: the IOP may be asleep waiting for exactly this
	}

	bool Thread::LagSpin(u64 cycle)
	{
		// A held or stopped IOP cannot catch up, and a CallOnEE function runs with the IOP blocked.
		if (m_mode != IopThreadMode::Threaded || !m_running || m_hold_depth > 0 || m_in_call)
			return true;

		m_lag_spins.Add();
		// What the IOP was doing when the wait began, read before the wake-up below: asleep (or on its
		// way), so the EE also waits for it to wake, or idle and awake, watching a watermark that gives it
		// less than S_min to run.
		const bool found_parked = m_sleep.sleeping.load(std::memory_order_relaxed);
		const bool found_idle = !found_parked && m_iop.idle.load(std::memory_order_relaxed);
		const u64 begin = IopClock::Now();
		m_ee.spinning.store(true, std::memory_order_relaxed);
		WakeIfParked(WakeSite::SpinEntry);

		bool reached = false;
		u64 iterations = 0;
		const EeSpinHint hint;
		const auto wait = [&](auto pause)
		{
			s64 lag = m_lag;
			u64 read_index = m_i2e.ReadIndex();
			for (;;)
			{
				pause();
				iterations++;
				if (m_i2e.MayHaveItems(read_index))
				{
					DrainI2E();
					lag = m_lag;
					read_index = m_i2e.ReadIndex();
				}
				const u64 pos = m_iop.pos_pub.load(std::memory_order_relaxed);
				if (static_cast<s64>(cycle - pos) <= lag)
				{
					// Acquire the release that published the completed work.
					std::atomic_thread_fence(std::memory_order_acquire);
					m_last_seen_iop_pos = pos;
					reached = true;
					break;
				}
				if ((iterations & (kSpinCheckInterval - 1)) == 0)
				{
					if (ShouldStop())
						break;
					WakeIfParked(WakeSite::SpinPeriodic);
					// Both hooks and I2E callbacks may change the EE-owned bound.
					lag = m_lag;
					read_index = m_i2e.ReadIndex();
				}
			}
		};
#if PS2_NATIVE_IOP_THREADED && defined(__aarch64__) && !defined(_MSC_VER)
		if (hint.use_isb)
			wait([] { asm volatile("isb" ::: "memory"); });
		else
			wait([] { asm volatile("yield" ::: "memory"); });
#else
		wait([&] { hint.Pause(); });
#endif
		m_ee.spinning.store(false, std::memory_order_relaxed);
		m_lag_spin_iterations.Add(iterations);
		const u64 stall_ns = IopClock::ElapsedNs(begin);
		m_lag_stall_ns.Add(stall_ns);
		if (found_parked)
		{
			m_lag_spins_parked.Add();
			m_lag_parked_stall_ns.Add(stall_ns);
		}
		else if (found_idle)
		{
			m_lag_spins_idle.Add();
			m_lag_idle_stall_ns.Add(stall_ns);
		}
		return reached;
	}

	bool Thread::PostE2I(u64 stamp, u16 kind, u32 arg)
	{
		if (!m_e2i.TryPush(stamp, kind, arg))
		{
			if (!WaitForE2ISpace(stamp, kind, arg))
				return false;
		}
		if (m_mode == IopThreadMode::Threaded)
		{
			// The watermark never trails a stamp in the ring, so the IOP learns of the item and may
			// run up to it. The push (the ring's release store) came first.
			if (stamp > m_ee_wm_local)
			{
				m_ee_wm_local = stamp;
				m_ee.wm.store(stamp, std::memory_order_release);
			}
			WakeIfParked(WakeSite::Post);
		}
		return true;
	}

	bool Thread::WaitForE2ISpace(u64 stamp, u16 kind, u32 arg)
	{
		// Making room takes the IOP consuming, which it cannot do while it is held, while it is
		// blocked in CallOnEE (this may be that call's function), or if it is not running.
		if (m_mode != IopThreadMode::Threaded || !m_running || m_hold_depth > 0 || m_in_call)
		{
			m_e2i_full_failfast.Add();
			return false;
		}

		m_e2i_full_spins.Add();
		m_ee.spinning.store(true, std::memory_order_relaxed);
		WakeIfParked(WakeSite::SpinEntry);

		bool queued = false;
		u64 iterations = 0;
		for (;;)
		{
			SpinHint();
			iterations++;
			if (m_i2e.MayHaveItems())
				DrainI2E();
			if (m_e2i.TryPush(stamp, kind, arg))
			{
				queued = true;
				break;
			}
			if ((iterations & (kSpinCheckInterval - 1)) == 0)
			{
				if (ShouldStop())
					break;
				WakeIfParked(WakeSite::SpinPeriodic);
			}
		}
		m_ee.spinning.store(false, std::memory_order_relaxed);
		m_e2i_full_iterations.Add(iterations);
		return queued;
	}

	void Thread::AcquireHold(HoldKind kind)
	{
#if PS2_NATIVE_IOP_THREADED
        // Never wait for the other CPU while owning its JIT writer gate.
        if(!ps2_mtvu_code_host_caller_idle())std::abort();
#endif
		// The IOP is already stopped for a hold taken inside another, inside a CallOnEE function, in
		// Lockstep (it only runs while the EE is in a handoff) and with no thread running, so those
		// only count.
		if (m_hold_depth > 0 || m_in_call || !m_running || m_mode != IopThreadMode::Threaded)
		{
			m_hold_depth++;
			m_hold_markers.Add();
			return;
		}

		m_hold_requests.Add();
		const u64 cycle = m_params.ee_cycle ? m_params.ee_cycle() : m_ee_wm_local;
		if (cycle > m_ee_wm_local)
		{
			m_ee_wm_local = cycle;
			m_ee.wm.store(cycle, std::memory_order_release);
		}

		// The generation first, relaxed; the target after it, release. The IOP reads them in the
		// opposite order, so a target it sees carries the generation that went with it.
		const u32 gen = ++m_hold_gen_local;
		m_ee.hold_gen.store(gen, std::memory_order_relaxed);
		m_ee.hold_target.store(cycle, std::memory_order_release);
		m_hold_requested = true;
		m_hold_pending = true;

		m_ee.spinning.store(true, std::memory_order_relaxed);
		WakeIfParked(WakeSite::SpinEntry);

		// A hold never gives up and never asks Params::should_stop (see ScopedHold). The IOP reaches the
		// hold after its current slice, and this loop serves its queue and CallOnEE functions meanwhile.
		const bool may_sleep = (kind == HoldKind::Lifecycle);
		const u64 begin = IopClock::Now();
		const u64 spin_budget = may_sleep ? IopClock::Budget(SPIN_TIME_NS) : 0;
		bool sleeping = false;
		u64 iterations = 0;
		const EeSpinHint hint;
		u64 read_index = m_i2e.ReadIndex();
		for (;;)
		{
			if (sleeping)
			{
				m_hold_sleeps.Add();
				std::this_thread::sleep_for(kPollSleep);
			}
			else
			{
				hint.Pause();
			}
			iterations++;
			if (m_iop.held_gen.load(std::memory_order_relaxed) == gen)
			{
				// Synchronize with TryHold() before the EE touches IOP state.
				std::atomic_thread_fence(std::memory_order_acquire);
				break;
			}
			if (m_i2e.MayHaveItems(read_index))
			{
				DrainI2E();
				read_index = m_i2e.ReadIndex();
			}
			if (sleeping)
			{
				if (iterations % kSleepingWakeInterval == 0)
				{
					WakeIfParked(WakeSite::SpinPeriodic);
					read_index = m_i2e.ReadIndex();
				}
			}
			else if ((iterations & (kSpinCheckInterval - 1)) == 0)
			{
				WakeIfParked(WakeSite::SpinPeriodic);
				read_index = m_i2e.ReadIndex();
				if (may_sleep && IopClock::Now() - begin >= spin_budget)
					sleeping = true;
			}
		}
		m_ee.spinning.store(false, std::memory_order_relaxed);
		m_hold_spin_iterations.Add(iterations);
		m_hold_pending = false;
		if (kind == HoldKind::Emulation)
		{
			m_emu_hold_requests.Add();
			m_emu_hold_stall_ns.Add(IopClock::ElapsedNs(begin));
		}

		m_hold_depth++;
		// The loop looks for the hold before it drains, so the IOP's last items may still be queued.
		// With the IOP stopped the queue cannot grow, so after this nothing it did before the hold
		// is waiting for the EE.
		DrainI2E();
		if (m_params.on_hold_acquired)
			m_params.on_hold_acquired();
	}

	void Thread::ReleaseHold()
	{
		pxAssertMsg(m_hold_depth > 0, "ScopedHold released with none held");
		if (--m_hold_depth != 0)
			return;
		// A hold taken inside a CallOnEE function that the spin of AcquireHold() ran is a counter
		// that starts and ends while the outer request is still waiting to be granted. Ending it must
		// leave that request alone.
		if (!m_hold_requested || m_hold_pending)
			return;
		m_hold_requested = false;
		// Pairs with the acquire load of hold_target in HoldWait(): what the EE wrote while it held
		// the IOP is visible to the IOP when it resumes.
		m_ee.hold_target.store(kNoHold, std::memory_order_release);
		if (!iz_iop_jit_notify(*this))
			m_sema.NotifyOfWork(); // not filtered
	}

	void Thread::Resync(u64 ee_cycle, u64 iop_pos)
	{
		pxAssertMsg(m_hold_depth > 0 || !m_running, "Resync() needs the IOP held or stopped");
		m_e2i.Reset();
		m_i2e.Reset();
		m_e2i_kicks.TakeAll();
		m_ee_wm_local = ee_cycle;
		m_iop_pos = iop_pos;
		m_last_seen_iop_pos = iop_pos;
		// Relaxed: the IOP reads these after the release that ends the hold (or, in Lockstep, after
		// the next handoff's request), which orders them.
		m_ee.wm.store(ee_cycle, std::memory_order_relaxed);
		m_iop.pos_pub.store(iop_pos, std::memory_order_relaxed);
	}

	u64 Thread::LockstepCall(u32 kind, u64 arg)
	{
		pxAssertMsg(m_running && m_mode == IopThreadMode::Lockstep, "a Lockstep handoff needs a running Lockstep thread");
		pxAssertMsg(!OnIopThread(), "a Lockstep handoff from the IOP thread");

		m_ls.kind = kind;
		m_ls.arg = arg;
		const u64 seq = ++m_req_seq_local;
		// The request is the plain writes above, released by this store. seq_cst, with the IOP's
		// seq_cst store of `sleeping` and re-check (ParkUntil), means a request cannot be slept through, so
		// the wake can be filtered by `sleeping` and WorkSema's state does not pile up wake-ups.
		m_ee.req_seq.store(seq, std::memory_order_seq_cst);
		if (m_sleep.sleeping.load(std::memory_order_seq_cst))
		{
			m_wakes_sent.Add();
			if (!iz_iop_jit_notify(*this))
				m_sema.NotifyOfWork();
		}

		// The EE is blocked here, and the IOP never waits for it in Lockstep, so this ends when the
		// request does. It does not look at the stop predicate: giving up halfway would leave the
		// IOP running against the EE.
		while (m_iop.done_seq.load(std::memory_order_acquire) != seq)
			SpinHint();
		m_handoffs.Add();
		return m_ls.result;
	}

	u64 Thread::LockstepRunSlice(u64 target)
	{
		return LockstepCall(kLsRunSlice, target);
	}

	size_t Thread::LockstepApplyUpTo(u64 stamp)
	{
		return static_cast<size_t>(LockstepCall(kLsApplyUpTo, stamp));
	}

	void Thread::LockstepLoop()
	{
		u64 done = 0;
		for (;;)
		{
			// Pairs with the release (seq_cst) store of req_seq in LockstepCall(): the request's
			// fields, and everything the EE did before it, are visible.
			const u64 seq = m_ee.req_seq.load(std::memory_order_acquire);
			if (seq != done)
			{
				// The EE is blocked in the handoff, so host commands may touch EE-owned state here
				// as IOP code may.
				if (m_host.HasPending())
					m_host.RunPending();

				if (m_ls.kind == kLsRunSlice)
				{
					const u64 reached = m_params.run_slice(m_iop_pos, m_ls.arg);
					pxAssertMsg(reached >= m_iop_pos, "run_slice moved the IOP backwards");
					m_iop_pos = std::max(reached, m_iop_pos);
					m_iop.pos_pub.store(m_iop_pos, std::memory_order_relaxed);
					m_ls.result = m_iop_pos;
				}
				else
				{
					u64 applied = 0;
					while (const E2IItem* head = m_e2i.Peek())
					{
						if (head->stamp > m_ls.arg)
							break;
						const E2IItem item = *head;
						m_e2i.Pop();
						if (m_params.apply_item)
							m_params.apply_item(item);
						m_items_applied.Add();
						applied++;
					}
					m_ls.result = applied;
				}

				done = seq;
				// Pairs with the acquire load of done_seq in LockstepCall(): what the IOP wrote while
				// it ran is visible to the EE once it returns.
				m_iop.done_seq.store(seq, std::memory_order_release);
				continue;
			}

			if (m_ee.shutdown.load(std::memory_order_acquire))
				break;
			ParkUntil([this, done] {
				return m_ee.req_seq.load(std::memory_order_seq_cst) != done || m_ee.shutdown.load(std::memory_order_seq_cst);
			});
		}
	}

	Thread::Ticket Thread::PostHostCommand(HostCommandQueue::Command command)
	{
		const Ticket ticket = m_host.Post(std::move(command));
		// Not filtered: a host thread cannot know whether the IOP is about to sleep, and commands are
		// rare (a disc change, a settings change).
		if (!iz_iop_jit_notify(*this))
			m_sema.NotifyOfWork();
		return ticket;
	}

	Thread::Ticket Thread::TryPostHostCommand(HostCommandQueue::Command command)
	{
		const Ticket ticket = m_host.TryPost(std::move(command));
		// Only an accepted command wakes the thread; a refused one is not in the queue.
		if (ticket != 0)
			if (!iz_iop_jit_notify(*this))
				m_sema.NotifyOfWork();
		return ticket;
	}

	void Thread::ReapplyFpcr(FPControlRegister value)
	{
		PostHostCommand([value] { FPControlRegister::SetCurrent(value); });
	}

	Stats Thread::GetStats() const
	{
		Stats s = {};
		s.lag_spins = m_lag_spins.Get();
		s.lag_spin_iterations = m_lag_spin_iterations.Get();
		s.lag_stall_ns = m_lag_stall_ns.Get();
		s.lag_spins_parked = m_lag_spins_parked.Get();
		s.lag_parked_stall_ns = m_lag_parked_stall_ns.Get();
		s.emu_hold_requests = m_emu_hold_requests.Get();
		s.emu_hold_stall_ns = m_emu_hold_stall_ns.Get();
		s.sif_ee_pumps = m_sif_ee_pumps.Get();
		s.hold_requests = m_hold_requests.Get();
		s.hold_markers = m_hold_markers.Get();
		s.hold_spin_iterations = m_hold_spin_iterations.Get();
		s.hold_sleeps = m_hold_sleeps.Get();
		s.e2i_full_spins = m_e2i_full_spins.Get();
		s.e2i_full_iterations = m_e2i_full_iterations.Get();
		s.e2i_full_failfast = m_e2i_full_failfast.Get();
		s.i2e_drained = m_i2e_drained.Get();
		s.calls_served = m_calls_served.Get();
		s.wakes_sent = m_wakes_sent.Get();
		s.wakes_dropped = m_wakes_dropped.Get();
		s.stop_checks = m_stop_checks.Get();
		s.handoffs = m_handoffs.Get();
		s.slices = m_slices.Get();
		s.items_applied = m_items_applied.Get();
		s.parks = m_parks.Get();
		s.holds_entered = m_holds_entered.Get();
		s.calls_posted = m_calls_posted.Get();
		s.calls_cancelled = m_calls_cancelled.Get();
		s.i2e_full_waits = m_i2e_full_waits.Get();
		s.kicks_applied = m_kicks_applied.Get();
		s.lag_spins_idle = m_lag_spins_idle.Get();
		s.lag_idle_stall_ns = m_lag_idle_stall_ns.Get();
		s.idle_waits = m_idle_waits.Get();
		s.idle_ns = m_idle_ns.Get();
		s.sleep_barrier_membarrier = m_use_membarrier ? 1 : 0;
		return s;
	}
} // namespace IopThread

// The Stats ABI stays unchanged. The optional post-join receipt uses this
// immutable hidden field mask; missing symbol on older cores means all fields.
#if PS2_NATIVE_IOP_THREADED
extern "C" __attribute__((visibility("hidden"))) const uint64_t
ps2_native_iop_stats_available_mask = UINT64_C(0x2ffffffff);
#endif
