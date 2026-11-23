"""Apply EE-owned, opt-in wall-time tracing to the exact R1216 sources."""
from pathlib import Path
import shutil

out = Path(__file__).resolve().parent
root = out.parents[3]
base = out.parent/'R1216-vif-gif-traffic'
dma = out.parent/'R1206-ee-dma-attribution'
for name in ('hosted-entry.c','engine.ld','ps2_dma_cpu_timing.hpp','ps2_frame_windows.hpp',
             'ps2_gta_cadence_trial.hpp','ps2_gta_frame_witness.hpp','ps2_mtvu_bulk_v4.hpp',
             'ps2_sb4_ee_warm.inc','checkpoint-stall-seed.h','verify-relocation.c','portable-image.py',
             'input.tile','build-frontend.py','package-app.cjs','verify-app.cjs'):
    shutil.copyfile(base/name,out/name)
for name in ('package-app.cjs','verify-app.cjs'):
    p = out/name
    p.write_text(p.read_text().replace('R1216','R1217'))
if not (out/'frontend-source').exists():
    shutil.copytree(base/'frontend-source',out/'frontend-source')

def only(text, before, after, expected=1):
    assert text.count(before)==expected, before
    return text.replace(before,after)

r5900=(dma/'R5900.cpp').read_text()
event=r5900.index('__fi void _cpuEventTest_Shared()\n{')
dispatch=r5900.index('template void _cpuEventTest_Shared',event)
body=r5900[event:dispatch]
body=only(body,'\n{\n','\n{\n\tEeGap::Scope trace(EeGap::Phase::Event);\n')
body=only(body,'\t\teeEventTestIsActive = false;\n\t\treturn;',
          '\t\teeEventTestIsActive = false;\n\t\ttrace.Close();\n\t\treturn;',2)
body=only(body,'\tiopRunSliceForEE<M>();','\tEeGap::Measure(EeGap::Phase::Iop, [] { iopRunSliceForEE<M>(); });')
body=only(body,'\t\trcntUpdate<M>();','\t\tEeGap::Measure(EeGap::Phase::Counters, [] { rcntUpdate<M>(); });')
body=only(body,'\tif (cpuRegs.interrupt)\n\t{','\tif (cpuRegs.interrupt)\n\t{\n\t\tEeGap::Scope dma(EeGap::Phase::Dma);')
body=only(body,'\t\telse\n\t\t\t_cpuTestInterrupts();','\t\telse\n\t\t\t_cpuTestInterrupts();\n\t\tdma.Close();')
a=body.index('\tif (VU0.VI[REG_VPU_STAT].UL & 0x1)')
b=body.index('\n\n\t// ---- Schedule Next Event Test',a)
body=body[:a]+'''	EeGap::Measure(EeGap::Phase::Vu, [] {
'''+''.join('\t'+line+'\n' for line in body[a:b].splitlines())+'\t});'+body[b:]
body=only(body,'\n\teeEventTestIsActive = false;\n}', '\n\teeEventTestIsActive = false;\n\ttrace.Close();\n}')
(out/'R5900.cpp').write_text('#include "ps2_ee_gap_trace.hpp"\n'+r5900[:event]+body+r5900[dispatch:])

canonical=root/'build/izps2-homebrew/source/pcsx2/Counters.cpp'
counters=canonical.read_text()
counters=only(counters,'\tDoFMVSwitch();\n\tVMManager::Internal::VSyncOnCPUThread();',
              '\tEeGap::Measure(EeGap::Phase::VsyncTasks, [] {\n\t\tDoFMVSwitch();\n\t\tVMManager::Internal::VSyncOnCPUThread();\n\t});')
for value in ('true','false'):
    counters=only(counters,f'\t\tVMManager::Internal::Throttle({value});',
                  f'\t\tEeGap::Measure(EeGap::Phase::Limiter, [] {{ VMManager::Internal::Throttle({value}); }});')
counters=only(counters,'\t\tgsPostVsyncStart();','\t\tEeGap::Measure(EeGap::Phase::GsSubmit, [] { gsPostVsyncStart(); });',2)
counters=only(counters,'\tVMManager::Internal::PollInputOnCPUThread();',
              '\tEeGap::Measure(EeGap::Phase::Input, [] { VMManager::Internal::PollInputOnCPUThread(); });')
(out/'Counters.cpp').write_text('#include "ps2_ee_gap_trace.hpp"\n'+counters)

windows=(base/'ps2_frame_windows.inc').read_text()
windows=only(windows,'#include "ps2_frame_windows.hpp"','#include "ps2_frame_windows.hpp"\n#include "ps2_ee_gap_trace.hpp"')
windows=only(windows,'uint64_t iz_video_accepted_presentations();', '''uint64_t iz_video_accepted_presentations();

EeGap::Trace* EeGap::active = nullptr;
std::uint64_t EeGap::Tick()
{
    return Common::Timer::GetCurrentValue();
}''')
windows=only(windows,'bool s_domain_timing_active;', '''bool s_domain_timing_active;
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
}''')
windows=only(windows,'            if (bytes > 0 && static_cast<size_t>(bytes) < sizeof(marker))\n                izps2_boot_stage(nullptr, marker, static_cast<uint32_t>(bytes));',
    '''            if (bytes > 0 && static_cast<size_t>(bytes) < sizeof(marker))
                EeGap::Measure(EeGap::Phase::VblankMarker,
                    [&] { izps2_boot_stage(nullptr, marker, static_cast<uint32_t>(bytes)); });''')
windows=only(windows,'    const auto tick = Common::Timer::GetCurrentValue();',
    '''    const auto tick = Common::Timer::GetCurrentValue();
    if (EeGap::active && !s_ee_gap_trace.OnVblank(frame, tick))
        EeGap::active = nullptr;
    EeGap::Scope sample_trace(EeGap::Phase::VblankSampling);''')
windows=only(windows,'            s_neutral_ee_cycle = cpuRegs.cycle;\n        }\n    }\n}',
             '            s_neutral_ee_cycle = cpuRegs.cycle;\n        }\n    }\n    sample_trace.Close();\n}')
(out/'ps2_frame_windows.inc').write_text(windows)

runner=(base/'ps2_vm_runner.cpp').read_text()
runner=only(runner,'    ps2_configure_vu1_wall_timing(capture_stalls);',
'''    ps2_configure_vu1_wall_timing(capture_stalls);
    const bool capture_gap_scopes =
#ifdef IZPS2_HOSTED_WORKER
        capture_stalls && hosted && module_flag(*hosted, "perf-ee-gap-scopes.flag");
#else
        false;
#endif''')
runner=only(runner,'    s_stall_peaks.Start(capture_stalls, Common::Timer::ConvertMillisecondsToValue(40));',
'''    s_stall_peaks.Start(capture_stalls, Common::Timer::ConvertMillisecondsToValue(40));
    if (capture_gap_scopes) {
        s_ee_gap_trace.Start(Common::Timer::ConvertMillisecondsToValue(40));
        EeGap::active = &s_ee_gap_trace;
    }''')
runner=only(runner,'            VMManager::Execute();',
                   '            EeGap::Measure(EeGap::Phase::Guest, [] { VMManager::Execute(); });')
runner=only(runner,'            s_stall_peaks.Pause();',
                   '            s_stall_peaks.Pause();\n            if (EeGap::active) s_ee_gap_trace.Discontinuity();')
runner=only(runner,'    if (control) report_frame_windows();',
'''    if (capture_gap_scopes) {
        EeGap::active = nullptr;
        report_ee_gap_trace();
    }
    if (control) report_frame_windows();''')
(out/'ps2_vm_runner.cpp').write_text(runner)
