"""Add opt-in samples around the exact Off-mode IOP owner used by R1235."""
from pathlib import Path

out = Path(__file__).resolve().parent
base = out.parent / 'R1217-ee-stall-partition'
source = (base / 'R5900.cpp').read_text()


def replace(old, new):
    global source
    assert source.count(old) == 1, old
    source = source.replace(old,new)


replace('#include "ps2_ee_gap_trace.hpp"', '''#include "ps2_ee_gap_trace.hpp"
#include "iop-slice-breakdown.h"''')
anchor = 'template <IopThreadMode M>\nstatic __fi void iopRunSliceForEEBody()'
replace(anchor, '''namespace
{
enum IopSlicePhase { Accrue, Execute, DeviceEvents, Total };

IopSliceBreakdown s_iop_slice_breakdown;
bool s_iop_slice_breakdown_enabled;
u32 s_iop_slice_breakdown_random;

// The rare branch provides a profiling specialization for Off mode. The
// other path instantiates the existing owner code without timing operations.
struct IopSliceSample
{
    const u64 start = GetCPUTicks();
    u64 last = start;

    void Boundary(IopSlicePhase phase)
    {
        const u64 now = GetCPUTicks();
        s_iop_slice_breakdown.phase_ticks[phase] += now - last;
        last = now;
    }

    ~IopSliceSample()
    {
        Boundary(DeviceEvents);
        s_iop_slice_breakdown.phase_ticks[Total] += last - start;
        ++s_iop_slice_breakdown.samples;
    }
};
}

void iz_ps2_configure_iop_slice_breakdown(bool enabled)
{
    s_iop_slice_breakdown_enabled = enabled;
    if (enabled)
    {
        s_iop_slice_breakdown = {};
        s_iop_slice_breakdown_random = 1;
        s_iop_slice_breakdown.tick_hz = GetTickFrequency();
    }
}

const IopSliceBreakdown& iz_ps2_get_iop_slice_breakdown()
{
    return s_iop_slice_breakdown;
}

template <IopThreadMode M, bool Sample = false>
static __fi void iopRunSliceForEEBody([[maybe_unused]] IopSliceSample* sample = nullptr)''')
anchor = '''\tif (EEsCycle > 0)
\t\tiopEventAction = true;

\tif constexpr (M == IopThreadMode::Lockstep)'''
replace(anchor, '''\tif (EEsCycle > 0)
\t\tiopEventAction = true;
    if constexpr (Sample)
        sample->Boundary(Accrue);

\tif constexpr (M == IopThreadMode::Lockstep)''')
anchor = '''#ifdef PS2_IOP_OWNER_IDLE_SLICE
\t\tif (M != IopThreadMode::Off || !iopTryOwnerIdleSlice(EEsCycle))
#endif
\t\t\tEEsCycle = psxCpu->ExecuteBlock(EEsCycle);
\t\tiopEventAction = false;
\t}

#ifdef PS2_IOP_QUIET_EE_EVENT
\tif constexpr (M == IopThreadMode::Off)
\t{
\t\tif (iopTryScheduleWithoutDeviceEvents())
\t\t\treturn;
#ifdef PS2_IOP_EVENT_FAST_ENTRY
\t\tiopEventTestWithPendingWork();'''
replace(anchor, '''#ifdef PS2_IOP_OWNER_IDLE_SLICE
        const bool handled = M == IopThreadMode::Off && iopTryOwnerIdleSlice(EEsCycle);
#else
        const bool handled = false;
#endif
        if (handled)
        {
            if constexpr (Sample)
                ++s_iop_slice_breakdown.idle_samples;
        }
        else
        {
            if constexpr (Sample)
                ++s_iop_slice_breakdown.jit_samples;
            EEsCycle = psxCpu->ExecuteBlock(EEsCycle);
        }
        iopEventAction = false;
    }
    if constexpr (Sample)
        sample->Boundary(Execute);

#ifdef PS2_IOP_QUIET_EE_EVENT
    if constexpr (M == IopThreadMode::Off)
    {
        if (iopTryScheduleWithoutDeviceEvents())
        {
            if constexpr (Sample)
                ++s_iop_slice_breakdown.quiet_event_samples;
            return;
        }
#ifdef PS2_IOP_EVENT_FAST_ENTRY
        if constexpr (Sample)
            ++s_iop_slice_breakdown.pending_event_samples;
        iopEventTestWithPendingWork();''')
anchor = '''template <IopThreadMode M>
static __fi void iopRunSliceForEE()
{'''
replace(anchor, '''template <IopThreadMode M>
static __fi void runIopSliceWithOptionalBreakdown()
{
    if constexpr (M == IopThreadMode::Off)
    {
        if (s_iop_slice_breakdown_enabled) [[unlikely]]
        {
            s_iop_slice_breakdown_random = s_iop_slice_breakdown_random * 1664525u + 1013904223u;
            if ((s_iop_slice_breakdown_random >> 21) == 0)
            {
                IopSliceSample sample;
                iopRunSliceForEEBody<M, true>(&sample);
                return;
            }
        }
    }
    iopRunSliceForEEBody<M>();
}

template <IopThreadMode M>
static __fi void iopRunSliceForEE()
{''')
replace('''\t\t\t\tiopRunSliceForEEBody<M>();''','''                runIopSliceWithOptionalBreakdown<M>();''')
replace('''#endif
\tiopRunSliceForEEBody<M>();
}

// The EE sets''','''#endif
    runIopSliceWithOptionalBreakdown<M>();
}

// The EE sets''')
(out / 'R5900.cpp').write_text(source)

package = out.parent / 'R1235-gta-checkpoint-compile-burst'
windows_header = (out.parent / 'R1221-ee-deferred-incoming/ps2_frame_windows.hpp').read_text()
source = windows_header
replace('#include "ps2_dma_cpu_timing.hpp"', '#include "ps2_dma_cpu_timing.hpp"\n#include "iop-slice-breakdown.h"')
replace('        Ps2DmaCpuTiming dma;', '        Ps2DmaCpuTiming dma;\n        IopSliceBreakdown iop_slice;')
(out/'ps2_frame_windows.hpp').write_text(source)
source = (package/'ps2_frame_windows.inc').read_text()
replace('        iz_ps2_dma_cpu_timing(),', '        iz_ps2_dma_cpu_timing(),\n        iz_ps2_get_iop_slice_breakdown(),')
replace('    const auto report_vu1 =', '''    const auto& a = first.iop_slice;
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
    const auto report_vu1 =''')
(out/'ps2_frame_windows.inc').write_text(source)
source = (package/'ps2_vm_runner.cpp').read_text()
replace('    const auto clear_dma_timing =', '''    const bool capture_iop_slice = hosted && module_flag(*hosted, "perf-iop-slice-breakdown.flag");
    iz_ps2_configure_iop_slice_breakdown(capture_iop_slice);
    const auto clear_iop_slice = ScopedGuard([] { iz_ps2_configure_iop_slice_breakdown(false); });
    const auto clear_dma_timing =''')
(out/'ps2_vm_runner.cpp').write_text(source)
