#include "PerformanceMetrics.h"
#include "ps2_frame_windows.hpp"
#include "ps2_ee_gap_trace.hpp"
#ifdef IZPS2_SWITCH2_SESSION_TIMING
#include "arm64/iR5900-arm64.h"
#include "ps2_startup_gap_probe.hpp"
#include "ps2_stall_peaks.hpp"
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
void iz_ps2_publish_deferred_ee_links();

EeGap::Trace* EeGap::active = nullptr;
std::uint64_t EeGap::Tick()
{
    return Common::Timer::GetCurrentValue();
}

namespace {

// The VM owner is the only writer. GS and presentation signals are cumulative.
Ps2FrameWindows s_frame_windows;

struct EeFieldProbe
{
    u32 field;
    u64 start_tick = 0;
    u64 end_tick = 0;
    EeCompileAudit begin{};
    EeCompileAudit end{};

    void Observe(u32 current, u64 tick)
    {
        if (current == field)
        {
            start_tick = tick;
            begin = recGetEeCompileAudit();
        }
        else if (current == field + 1 && start_tick)
        {
            end_tick = tick;
            end = recGetEeCompileAudit();
        }
    }

    void Report(u64 hz) const
    {
        if (end_tick > start_tick && end.blocks >= begin.blocks && end.ticks >= begin.ticks && hz)
            Console.WriteLn("IZPS2_EE_TARGET_FIELD start=%u wall_ms=%.6f ee_blocks=%llu ee_compile_ms=%.6f",
                field, Common::Timer::ConvertValueToMilliseconds(end_tick - start_tick),
                static_cast<unsigned long long>(end.blocks - begin.blocks), 1000.0 * (end.ticks - begin.ticks) / hz);
    }
};
EeFieldProbe s_ee_target_probes[] = { {5170}, {6212} };
bool s_domain_timing_active;
EeGap::Trace s_ee_gap_trace;

void report_ee_gap_trace()
{
    Console.WriteLn("IZPS2_EE_GAP_TRACE intervals=%u qualifying=%u retained=%u",
        s_ee_gap_trace.Intervals(), s_ee_gap_trace.Qualified(), s_ee_gap_trace.Count());
    for (unsigned i = 0; i < s_ee_gap_trace.Count(); ++i)
    {
        const auto& frame = s_ee_gap_trace.Get(i);
        Console.WriteLn("IZPS2_EE_GAP start=%u total_ms=%.3f", frame.start_frame,
            Common::Timer::ConvertValueToMilliseconds(frame.elapsed));
        for (unsigned phase = 0; phase < frame.spans.size(); ++phase)
        {
            const auto& span = frame.spans[phase];
            if (!span.total)
                continue;
            Console.WriteLn("IZPS2_EE_GAP_SCOPE start=%u scope=%s exclusive_ms=%.3f max_segment_ms=%.3f",
                frame.start_frame, EeGap::PhaseNames[phase],
                Common::Timer::ConvertValueToMilliseconds(span.total),
                Common::Timer::ConvertValueToMilliseconds(span.max_segment));
        }
    }
}
#ifdef IZPS2_SWITCH2_SESSION_TIMING
Ps2StartupGapProbe<Ps2FrameWindows::Timing, Ps2FrameWindows::Intervals> s_startup_gap;
Ps2StallPeaks<Ps2FrameWindows::Timing, 16> s_stall_peaks;
#endif

#ifdef IZPS2_SWITCH2_SESSION_TIMING
Ps2DomainTiming sample_run_domains()
{
    Ps2DomainTiming sample;
    if (!s_domain_timing_active)
        return sample;
    iz_ps2_get_ee_domain_timing(sample.ee_event, sample.ee_compile);
    sample.iop = iz_ps2_iop_cpu_ticks();
    sample.vu1 = iz_ps2_vu1_cpu_ticks();
    if (sample.iop.handle) {
        const auto stats = IopThread::g_thread.GetStats();
        sample.iop_lag_ns = stats.lag_stall_ns;
        sample.iop_hold_ns = stats.emu_hold_stall_ns;
        sample.iop_sif_calls = stats.calls_served;
    }
    iz_ps2_get_sd_read_timing(&sample.sd_calls, &sample.sd_ticks, &sample.sd_bytes);
    return sample;
}

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
        ee.blocks, ee.ticks, jit.calls, jit.bytes, jit.ticks, ps2_vu1_cpu_timing(),
        ps2_vu1_wall_timing(), ps2_publication_timing(),
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
        sample_run_domains(),
        iz_ps2_dma_cpu_timing(),
        iz_ps2_get_iop_slice_breakdown(),
    };
}

void report_run_domains(const char* phase, u32 frame, const Ps2DomainTiming& first,
    const Ps2DomainTiming& last, uint64_t cpu_hz, uint64_t wall_hz)
{
    const auto scopes = [&](const char* name, const Ps2SampleTotals& a, const Ps2SampleTotals& b) {
        const auto calls = b.calls - a.calls;
        const auto selected = b.selected - a.selected;
        const auto valid = b.valid - a.valid;
        const double cpu = 1000.0 * (b.cpu - a.cpu) / cpu_hz;
        const double wall = 1000.0 * (b.wall - a.wall) / wall_hz;
        Console.WriteLn("IZPS2_EE_DOMAIN phase=%s start=%u scope=%s calls=%llu selected=%llu samples=%llu "
            "sampled_cpu_ms=%.6f sampled_wall_ms=%.6f",
            phase, frame, name, static_cast<unsigned long long>(calls), static_cast<unsigned long long>(selected),
            static_cast<unsigned long long>(valid), cpu, wall);
    };
    scopes("event", first.ee_event, last.ee_event);
    scopes("compile", first.ee_compile, last.ee_compile);

    const auto worker = [&](const char* name, const Ps2WorkerCpuTicks& a, const Ps2WorkerCpuTicks& b) {
        const bool valid = a.handle && a.handle == b.handle && !a.result && !b.result && b.ticks >= a.ticks;
        Console.WriteLn("IZPS2_WORKER_CPU phase=%s start=%u worker=%s valid=%d rc_begin=0x%08x rc_end=0x%08x cpu_ms=%.3f",
            phase, frame, name, int(valid), a.result, b.result, valid ? 1000.0 * (b.ticks - a.ticks) / wall_hz : 0.0);
    };
    worker("iop", first.iop, last.iop);
    worker("vu1", first.vu1, last.vu1);
    if (first.iop.handle && first.iop.handle == last.iop.handle &&
        last.iop_lag_ns >= first.iop_lag_ns && last.iop_hold_ns >= first.iop_hold_ns && last.iop_sif_calls >= first.iop_sif_calls) {
        Console.WriteLn("IZPS2_IOP_WAIT_WINDOW phase=%s start=%u ee_lag_wall_ms=%.3f ee_hold_wall_ms=%.3f serviced_calls=%llu",
            phase, frame, (last.iop_lag_ns - first.iop_lag_ns) / 1000000.0,
            (last.iop_hold_ns - first.iop_hold_ns) / 1000000.0,
            static_cast<unsigned long long>(last.iop_sif_calls - first.iop_sif_calls));
    }
    Console.WriteLn("IZPS2_SD_READ_WINDOW phase=%s start=%u calls=%llu bytes=%llu summed_wall_ms=%.3f",
        phase, frame, static_cast<unsigned long long>(last.sd_calls - first.sd_calls),
        static_cast<unsigned long long>(last.sd_bytes - first.sd_bytes), 1000.0 * (last.sd_ticks - first.sd_ticks) / wall_hz);
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
    if (s_domain_timing_active)
        report_run_domains(phase, frame, first.domains, last.domains, cpu_hz, system_hz);
    if (last.dma.calls > first.dma.calls)
    {
        const auto delta_ms = [&](Ps2DmaCpuTiming::Scope scope) {
            return 1000.0 * (last.dma.ticks[scope] - first.dma.ticks[scope]) / cpu_hz;
        };
        Console.WriteLn("IZPS2_EE_DMA_TIME phase=%s start=%u calls=%llu samples=%llu "
                        "sampled_total_ms=%.6f sampled_vif1_ms=%.6f sampled_gif_ms=%.6f sampled_other_ms=%.6f",
            phase, frame, static_cast<unsigned long long>(last.dma.calls - first.dma.calls),
            static_cast<unsigned long long>(last.dma.samples - first.dma.samples),
            delta_ms(Ps2DmaCpuTiming::Scan), delta_ms(Ps2DmaCpuTiming::Vif1),
            delta_ms(Ps2DmaCpuTiming::Gif), delta_ms(Ps2DmaCpuTiming::Other));
    }
    const auto& a = first.iop_slice;
    const auto& b = last.iop_slice;
    if (b.samples > a.samples && b.tick_hz && (!a.tick_hz || b.tick_hz == a.tick_hz))
    {
        const auto ms = [&](unsigned scope) { return 1000.0 * (b.phase_ticks[scope] - a.phase_ticks[scope]) / b.tick_hz; };
        Console.WriteLn("IZPS2_IOP_SLICE_BREAKDOWN phase=%s start=%u samples=%llu idle=%llu jit=%llu "
                        "quiet=%llu pending=%llu sampled_accrue_ms=%.6f sampled_execute_ms=%.6f "
                        "sampled_events_ms=%.6f sampled_total_ms=%.6f", phase, frame,
            static_cast<unsigned long long>(b.samples - a.samples),
            static_cast<unsigned long long>(b.idle_samples - a.idle_samples),
            static_cast<unsigned long long>(b.jit_samples - a.jit_samples),
            static_cast<unsigned long long>(b.quiet_event_samples - a.quiet_event_samples),
            static_cast<unsigned long long>(b.pending_event_samples - a.pending_event_samples),
            ms(0), ms(1), ms(2), ms(3));
    }
    const auto report_vu1 = [&](const char* path, const Ps2Vu1Sample& begin, const Ps2Vu1Sample& end) {
        if (end.calls < begin.calls || end.selected < begin.selected ||
            end.measured < begin.measured || end.cpu_ticks < begin.cpu_ticks)
            return;
        const auto calls = end.calls - begin.calls;
        const auto samples = end.measured - begin.measured;
        const double sampled_ms = 1000.0 * (end.cpu_ticks - begin.cpu_ticks) / cpu_hz;
        Console.WriteLn("IZPS2_VU1_CPU_WINDOW phase=%s start=%u path=%s mtvu=%u calls=%llu selected=%llu "
            "samples=%llu sampled_cpu_ms=%.6f estimated_cpu_ms=%.3f",
            phase, frame, path, unsigned(THREAD_VU1), static_cast<unsigned long long>(calls),
            static_cast<unsigned long long>(end.selected - begin.selected),
            static_cast<unsigned long long>(samples), sampled_ms, samples ? sampled_ms * calls / samples : 0.0);
    };
    report_vu1("execute", first.vu1.execute, last.vu1.execute);
    report_vu1("xgkick", first.vu1.xgkick, last.vu1.xgkick);
    Console.WriteLn("IZPS2_SESSION_TIMING phase=%s start=%u wall_ms=%.3f ee_thread_cpu_ms=%.3f "
        "ee_blocks=%llu ee_compile_wall_ms=%.3f jit_calls=%llu jit_bytes=%llu jit_client_ms=%.3f",
        phase, frame, Common::Timer::ConvertValueToMilliseconds(last.wall_ticks - first.wall_ticks),
        1000.0 * (last.ee_cpu_ticks - first.ee_cpu_ticks) / cpu_hz,
        static_cast<unsigned long long>(last.ee_blocks - first.ee_blocks),
        1000.0 * (last.ee_compile_ticks - first.ee_compile_ticks) / system_hz,
        static_cast<unsigned long long>(last.jit_calls - first.jit_calls),
        static_cast<unsigned long long>(last.jit_bytes - first.jit_bytes),
        1000.0 * (last.jit_ticks - first.jit_ticks) / system_hz);
    const auto elapsed_ms = [system_hz](uint64_t begin, uint64_t end) {
        return end >= begin ? 1000.0 * (end - begin) / system_hz : 0.0;
    };
    const auto& before = first.publication;
    const auto& after = last.publication;
    if (THREAD_VU1 && after.vu1_calls >= before.vu1_calls)
    {
        Console.WriteLn("IZPS2_JIT_OWNER_WINDOW phase=%s start=%u vu1_calls=%llu vu1_sdk_ms=%.3f "
            "vu1_mutex_ms=%.3f primary_mutex_ms=%.3f",
            phase, frame, static_cast<unsigned long long>(after.vu1_calls - before.vu1_calls),
            elapsed_ms(before.vu1_ticks, after.vu1_ticks), elapsed_ms(before.vu1_wait_ticks, after.vu1_wait_ticks),
            elapsed_ms(before.primary_wait_ticks, after.primary_wait_ticks));
    }
    if (s_stall_peaks.Recording() && last.vu1_wall.jit_episodes >= first.vu1_wall.jit_episodes)
    {
        Console.WriteLn("IZPS2_VU1_WALL_WINDOW phase=%s start=%u mtvu=%u execute_wall_ms=%.3f "
            "jit_episodes=%llu jit_episode_wall_ms=%.3f",
            phase, frame, unsigned(THREAD_VU1),
            elapsed_ms(first.vu1_wall.execute_ticks, last.vu1_wall.execute_ticks),
            static_cast<unsigned long long>(last.vu1_wall.jit_episodes - first.vu1_wall.jit_episodes),
            elapsed_ms(first.vu1_wall.jit_episode_ticks, last.vu1_wall.jit_episode_ticks));
    }

#ifdef IZPS2_SWITCH2_NATIVE_POLL_TRIAL
    if (last.poll_skips > first.poll_skips && last.poll_guest_cycles >= first.poll_guest_cycles)
        Console.WriteLn("IZPS2_EE_POLL_WINDOW phase=%s start=%u skips=%llu skipped_guest_cycles=%llu",
            phase, frame, static_cast<unsigned long long>(last.poll_skips - first.poll_skips),
            static_cast<unsigned long long>(last.poll_guest_cycles - first.poll_guest_cycles));
#endif
#ifdef IZPS2_SWITCH2_QUEUE_WAIT_AUDIT
    if (last.gs_wait_ticks >= first.gs_wait_ticks && last.vu1_wait_ticks >= first.vu1_wait_ticks)
        Console.WriteLn("IZPS2_QUEUE_WAIT_WINDOW phase=%s start=%u gs_wait_ms=%.3f vu1_wait_ms=%.3f",
            phase, frame, elapsed_ms(first.gs_wait_ticks, last.gs_wait_ticks),
            elapsed_ms(first.vu1_wait_ticks, last.vu1_wait_ticks));
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
    if (s_stall_peaks.Recording())
    {
        Console.WriteLn("IZPS2_STALLS threshold_ms=40 qualifying=%llu recorded=%zu",
            static_cast<unsigned long long>(s_stall_peaks.Qualified()), s_stall_peaks.Count());
        for (size_t i = 0; i < s_stall_peaks.Count(); ++i)
        {
            const auto& span = s_stall_peaks.Get(i);
            Console.WriteLn("IZPS2_STALL start=%u wall_ms=%.3f", span.frame,
                Common::Timer::ConvertValueToMilliseconds(span.ticks));
            report_session_timing("long_gap", span.frame, span.begin, span.end);
        }
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
#ifdef IZPS2_SWITCH2_GTA_FRAME_WITNESS
        const auto& first = window.first.gta_witness;
        const auto& last = window.last.gta_witness;
        if (first.enabled && last.enabled && last.commits >= first.commits)
        {
            const auto elapsed_ms = Common::Timer::ConvertValueToMilliseconds(window.elapsed);
            const auto frames = last.commits - first.commits;
            Console.WriteLn("IZPS2_GTA_GAME_FRAMES start=%u vblanks=%zu new_buffers=%llu hz=%.3f",
                window.first_frame, Ps2FrameWindows::Intervals, static_cast<unsigned long long>(frames),
                1000.0 * frames / elapsed_ms);
        }
#endif
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
#if defined(IZPS2_HOSTED_WORKER)
    if (!s_hosted_guest_vblank) {
        const unsigned fields = ++s_hosted_startup_fields;
        if (fields == 1 || fields == 4 || fields == 32 || fields == 120) {
            char marker[112];
            const int bytes = std::snprintf(marker, sizeof(marker),
                "25 guest vblanks=%u presents=%llu game frame=%u\n",
                fields, static_cast<unsigned long long>(iz_video_accepted_presentations()), frame);
            if (bytes > 0 && static_cast<size_t>(bytes) < sizeof(marker))
                EeGap::Measure(EeGap::Phase::VblankMarker,
                    [&] { izps2_boot_stage(nullptr, marker, static_cast<uint32_t>(bytes)); });
        }
        s_hosted_guest_vblank = fields >= 120;
    }
#endif
    const auto tick = Common::Timer::GetCurrentValue();
    if (s_neutral_replay.state == Ps2NeutralReplay::State::Running)
        for (auto& probe : s_ee_target_probes) probe.Observe(frame, tick);
    if (EeGap::active && !s_ee_gap_trace.OnVblank(frame, tick))
        EeGap::active = nullptr;
    EeGap::Scope sample_trace(EeGap::Phase::VblankSampling);
    if (iz_ps2_ee_span_trace) iz_ps2_ee_span_trace->SetField(frame);
    iz_ps2_publish_deferred_ee_links();
#ifdef IZPS2_SWITCH2_SESSION_TIMING
    const bool sample_now = s_startup_gap.Recording() || s_stall_peaks.Recording();
    Ps2FrameWindows::Timing timing{};
    if (sample_now)
    {
        timing = sample_session_timing();
        s_startup_gap.Add(frame, tick, [&timing] { return timing; });
        s_stall_peaks.Add(frame, tick, timing);
    }
#endif
    s_frame_windows.Add(frame, tick, [&] {
        const auto signals = PerformanceMetrics::GetGameFrameSignals();
        return Ps2FrameWindows::Counts{
            .presents = iz_video_accepted_presentations(),
            .register_updates = signals.register_updates,
            .blits = signals.framebuffer_blits,
            .nominal_hz = VMManager::GetFrameRate(),
#ifdef IZPS2_SWITCH2_GTA_FRAME_WITNESS
            .gta_witness = iz_ps2_gta_frame_witness(),
#endif
#ifdef IZPS2_SWITCH2_VK_STREAM_PRESSURE
            .vk_streams = g_ps2_vk_stream_pressure.Read(),
#endif
#ifdef IZPS2_SWITCH2_SESSION_TIMING
            .timing = sample_now ? timing : sample_session_timing(),
#endif
        };
    });
    if (s_neutral_replay.state == Ps2NeutralReplay::State::Running) {
        s_neutral_replay.Observe(frame, tick);
        if (s_neutral_replay.state == Ps2NeutralReplay::State::Complete) {
            s_neutral_ee_crc = crc32(0, eeMem->Main, Ps2MemSize::ExposedRam);
            s_neutral_ee_pc = cpuRegs.pc;
            s_neutral_ee_cycle = cpuRegs.cycle;
        }
    }
    sample_trace.Close();
}
