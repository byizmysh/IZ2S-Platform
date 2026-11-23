#include "PerformanceMetrics.h"
#include "ps2_frame_windows.hpp"
#ifdef IZPS2_SWITCH2_SESSION_TIMING
#include "arm64/iR5900-arm64.h"
#include "ps2_startup_gap_probe.hpp"
#endif
#ifdef IZPS2_SWITCH2_IOP_SESSION_TIMING
#include "iop-off-timing-private.h"
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
#include "EECycleRateSampler.h"
#endif

#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
namespace NativePoll {
void GetTrialCounters(uint64_t& skips, uint64_t& cycles);
}
#endif

uint64_t iz_video_accepted_presentations();

namespace {

// The VM owner is the only writer. GS and presentation signals are cumulative.
Ps2FrameWindows s_frame_windows;
#ifdef IZPS2_SWITCH2_SESSION_TIMING
Ps2StartupGapProbe<Ps2FrameWindows::Timing, Ps2FrameWindows::Intervals> s_startup_gap;
#endif

#ifdef IZPS2_SWITCH2_SESSION_TIMING
Ps2FrameWindows::Timing sample_session_timing()
{
    const auto ee = recGetEeCompileAudit();
    const auto jit = HostSys::GetJitCacheTiming();
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
    uint64_t poll_skips, poll_guest_cycles;
    NativePoll::GetTrialCounters(poll_skips, poll_guest_cycles);
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
    const auto queue_wait = EECycleRateSampler::GetFrameWindowWaitTally();
#endif
#ifdef IZPS2_SWITCH2_IOP_SESSION_TIMING
    Ps2IopOffSampleTiming iop{};
    ps2_iop_off_sample_timing_snapshot(&iop);
#endif
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
    Ps2IopOffSampleTiming ee_event{};
    ps2_ee_event_timing_snapshot(&ee_event);
#endif
    return {Common::Timer::GetCurrentValue(), Threading::GetThreadCpuTime(),
        ee.blocks, ee.ticks, jit.calls, jit.bytes, jit.ticks,
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
        poll_skips, poll_guest_cycles,
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
        queue_wait.gs_wait_ticks, queue_wait.vu1_wait_ticks,
#endif
#ifdef IZPS2_SWITCH2_LIMITER_AUDIT
        limiter_ticks, limiter_calls, VMManager::GetTargetSpeed(), VMManager::IsUsingVSyncForTiming(),
#endif
#ifdef IZPS2_SWITCH2_IOP_SESSION_TIMING
        ee.publication_ticks, iop.calls, iop.selected, iop.valid, iop.total_ticks, iop.tick_hz,
#endif
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
        ee_event.calls, ee_event.valid, ee_event.total_ticks, ee_event.tick_hz,
#endif
    };
}

void report_session_timing(const char* phase, u32 frame, const Ps2FrameWindows::Timing& first,
    const Ps2FrameWindows::Timing& last)
{
    if (last.wall_ticks < first.wall_ticks || last.ee_cpu_ticks < first.ee_cpu_ticks ||
        last.ee_blocks < first.ee_blocks || last.ee_compile_ticks < first.ee_compile_ticks ||
        last.jit_calls < first.jit_calls || last.jit_bytes < first.jit_bytes || last.jit_ticks < first.jit_ticks)
        return;

    const uint64_t cpu_hz = Threading::GetThreadTicksPerSecond();
    const uint64_t system_hz = iz_tick_frequency();
    if (!cpu_hz || !system_hz)
        return;
    Console.WriteLn("IZPS2_SESSION_TIMING phase=%s start=%u wall_ms=%.3f ee_thread_cpu_ms=%.3f "
        "ee_blocks=%llu ee_compile_wall_ms=%.3f jit_calls=%llu jit_bytes=%llu jit_client_ms=%.3f",
        phase, frame, Common::Timer::ConvertValueToMilliseconds(last.wall_ticks - first.wall_ticks),
        1000.0 * (last.ee_cpu_ticks - first.ee_cpu_ticks) / cpu_hz,
        static_cast<unsigned long long>(last.ee_blocks - first.ee_blocks),
        1000.0 * (last.ee_compile_ticks - first.ee_compile_ticks) / system_hz,
        static_cast<unsigned long long>(last.jit_calls - first.jit_calls),
        static_cast<unsigned long long>(last.jit_bytes - first.jit_bytes),
        1000.0 * (last.jit_ticks - first.jit_ticks) / system_hz);
#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
    if (last.poll_skips > first.poll_skips && last.poll_guest_cycles >= first.poll_guest_cycles)
        Console.WriteLn("IZPS2_EE_POLL_WINDOW phase=%s start=%u skips=%llu skipped_guest_cycles=%llu",
            phase, frame, static_cast<unsigned long long>(last.poll_skips - first.poll_skips),
            static_cast<unsigned long long>(last.poll_guest_cycles - first.poll_guest_cycles));
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
    if (last.gs_wait_ticks >= first.gs_wait_ticks && last.vu1_wait_ticks >= first.vu1_wait_ticks)
        Console.WriteLn("IZPS2_QUEUE_WAIT_WINDOW phase=%s start=%u gs_wait_ms=%.3f vu1_wait_ms=%.3f",
            phase, frame, Common::Timer::ConvertValueToMilliseconds(last.gs_wait_ticks - first.gs_wait_ticks),
            Common::Timer::ConvertValueToMilliseconds(last.vu1_wait_ticks - first.vu1_wait_ticks));
#endif
#ifdef IZPS2_SWITCH2_LIMITER_AUDIT
    if (last.limiter_ticks >= first.limiter_ticks && last.limiter_calls >= first.limiter_calls)
        Console.WriteLn("IZPS2_LIMITER_WINDOW phase=%s start=%u calls=%llu throttle_wall_ms=%.3f "
            "first_target_speed=%.3f last_target_speed=%.3f first_host_vsync_pacing=%d last_host_vsync_pacing=%d",
            phase, frame, static_cast<unsigned long long>(last.limiter_calls - first.limiter_calls),
            1000.0 * (last.limiter_ticks - first.limiter_ticks) / system_hz,
            first.target_speed, last.target_speed, first.host_vsync_pacing, last.host_vsync_pacing);
#endif
#ifdef IZPS2_SWITCH2_IOP_SESSION_TIMING
    if (last.ee_compile_publication_ticks >= first.ee_compile_publication_ticks)
    {
        const uint64_t elapsed = last.ee_compile_ticks - first.ee_compile_ticks;
        const uint64_t publishing = last.ee_compile_publication_ticks - first.ee_compile_publication_ticks;
        if (publishing <= elapsed)
            Console.WriteLn("IZPS2_EE_COMPILE_SPLIT phase=%s start=%u nonpublish_wall_ms=%.3f publish_client_ms=%.3f",
                phase, frame, 1000.0 * (elapsed - publishing) / system_hz, 1000.0 * publishing / system_hz);
    }

    if (last.iop_calls >= first.iop_calls && last.iop_selected >= first.iop_selected &&
        last.iop_valid >= first.iop_valid && last.iop_ticks >= first.iop_ticks &&
        (!first.iop_tick_hz || first.iop_tick_hz == last.iop_tick_hz))
    {
        const auto calls = last.iop_calls - first.iop_calls;
        const auto selected = last.iop_selected - first.iop_selected;
        const auto valid = last.iop_valid - first.iop_valid;
        if (valid <= selected && selected <= calls)
        {
            const bool timing_available = valid != 0 && last.iop_tick_hz != 0;
            const double sampled_wall_ms = timing_available ?
                1000.0 * (last.iop_ticks - first.iop_ticks) / last.iop_tick_hz : 0.0;
            Console.WriteLn("IZPS2_IOP_OFF_WINDOW phase=%s start=%u calls=%llu selected=%llu valid=%llu "
                "sampled_wall_ms=%.6f timing_available=%d",
                phase, frame, static_cast<unsigned long long>(calls), static_cast<unsigned long long>(selected),
                static_cast<unsigned long long>(valid), sampled_wall_ms, timing_available ? 1 : 0);
        }
    }
#endif
#ifdef IZPS2_SWITCH2_EE_EVENT_AUDIT
    if (last.ee_event_calls > first.ee_event_calls && last.ee_event_valid >= first.ee_event_valid &&
        last.ee_event_ticks >= first.ee_event_ticks && last.ee_event_tick_hz &&
        (!first.ee_event_tick_hz || first.ee_event_tick_hz == last.ee_event_tick_hz))
    {
        Console.WriteLn("IZPS2_EE_EVENT_WINDOW phase=%s start=%u calls=%llu valid=%llu sampled_wall_ms=%.6f",
            phase, frame, static_cast<unsigned long long>(last.ee_event_calls - first.ee_event_calls),
            static_cast<unsigned long long>(last.ee_event_valid - first.ee_event_valid),
            1000.0 * (last.ee_event_ticks - first.ee_event_ticks) / last.ee_event_tick_hz);
    }
#endif
}
#endif

#ifdef IZPS2_SWITCH2_EAGER_AB
#include "ps2_eager_ab.inc"
#endif

void report_frame_windows()
{
    s_frame_windows.Stop();
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    if (const auto* peak = s_startup_gap.CompletedPeak()) {
        Console.WriteLn("IZPS2_STARTUP_GAP start=%u wall_ms=%.3f", peak->start_frame,
            Common::Timer::ConvertValueToMilliseconds(peak->ticks));
        report_session_timing("startup_peak", peak->start_frame, peak->first, peak->last);
    }
#endif
    for (size_t i = 0; i < s_frame_windows.Count(); ++i)
    {
        const auto& window = s_frame_windows.Get(i);
        if (!window.elapsed || window.last.nominal_hz != window.first.nominal_hz ||
            window.last.presents < window.first.presents ||
            window.last.register_updates < window.first.register_updates ||
            window.last.blits < window.first.blits)
            continue;
        const auto [p50, p95, p99] = window.Percentiles();
        Console.WriteLn(
            "IZPS2_FRAME_WINDOW start=%u vblanks=%zu wall_ms=%.3f nominal_hz=%.3f wall_start_tick=%llu "
            "accepted_presents=%llu gs_register_updates=%llu framebuffer_blits=%llu "
            "vblank_p50_ms=%.3f vblank_p95_ms=%.3f vblank_p99_ms=%.3f vblank_max_ms=%.3f",
            window.first_frame, Ps2FrameWindows::Intervals,
            Common::Timer::ConvertValueToMilliseconds(window.elapsed), window.first.nominal_hz,
            static_cast<unsigned long long>(window.first_tick),
            static_cast<unsigned long long>(window.last.presents - window.first.presents),
            static_cast<unsigned long long>(window.last.register_updates - window.first.register_updates),
            static_cast<unsigned long long>(window.last.blits - window.first.blits),
            Common::Timer::ConvertValueToMilliseconds(p50),
            Common::Timer::ConvertValueToMilliseconds(p95),
            Common::Timer::ConvertValueToMilliseconds(p99),
            Common::Timer::ConvertValueToMilliseconds(window.MaxInterval()));
#ifdef IZPS2_SWITCH2_SESSION_TIMING
        report_session_timing("gameplay", window.first_frame, window.first.timing, window.last.timing);
#endif
#ifdef IZPS2_SWITCH2_VK_STREAM_PRESSURE
        using Pressure = Ps2VkStreamPressure;
        for (size_t ring = 0; ring < Pressure::Names.size(); ++ring)
        {
            Pressure::Counts changes{};
            for (size_t event = 0; event < Pressure::EventCount; ++event)
                changes[event] = window.last.vk_streams[ring][event] - window.first.vk_streams[ring][event];
            if (std::all_of(changes.begin(), changes.end(), [](uint64_t count) { return count == 0; }))
                continue;
            Console.WriteLn("IZPS2_VK_STREAM_WINDOW start=%u ring=%s fence_waits=%llu fence_ms=%.3f "
                "reserve_misses=%llu grows=%llu grow_failures=%llu",
                window.first_frame, Pressure::Names[ring],
                static_cast<unsigned long long>(changes[Pressure::FenceWait]),
                Common::Timer::ConvertValueToMilliseconds(changes[Pressure::FenceTicks]),
                static_cast<unsigned long long>(changes[Pressure::ReserveMiss]),
                static_cast<unsigned long long>(changes[Pressure::Grew]),
                static_cast<unsigned long long>(changes[Pressure::GrowFailed]));
        }
#endif
    }
}

} // namespace

void iz_ps2_record_ee_vblank(u32 frame)
{
#ifdef IZPS2_SWITCH2_EAGER_AB
    if (s_eager_ab)
    {
        eager_ab_record(frame, Common::Timer::GetCurrentValue());
        return;
    }
#endif
    if (!s_frame_windows.Recording())
        return;
    const auto tick = Common::Timer::GetCurrentValue();
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    if (s_startup_gap.Recording())
        s_startup_gap.Add(frame, tick, sample_session_timing);
#endif
    s_frame_windows.Add(frame, tick, [] {
        const auto signals = PerformanceMetrics::GetGameFrameSignals();
        return Ps2FrameWindows::Counts{
            .presents = iz_video_accepted_presentations(),
            .register_updates = signals.register_updates,
            .blits = signals.framebuffer_blits,
            .nominal_hz = VMManager::GetFrameRate(),
#ifdef IZPS2_SWITCH2_VK_STREAM_PRESSURE
            .vk_streams = g_ps2_vk_stream_pressure.Read(),
#endif
#ifdef IZPS2_SWITCH2_SESSION_TIMING
            .timing = sample_session_timing(),
#endif
        };
    });
}
