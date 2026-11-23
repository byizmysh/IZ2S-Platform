"""Apply the late-burst learner to the exact linked EE emitter and VM owner."""
from pathlib import Path
import json
import shutil
import subprocess

out = Path(__file__).resolve().parent
base = out.parent / 'R1247-vu-cache-cold-path'
recipe = json.loads((base / 'compile-recipes.json').read_text())['iR5900-arm64.cpp']
emitter_source = Path(recipe['argv'][recipe['argv'].index('-c') + 1])
emitter_object = Path(recipe['argv'][recipe['argv'].index('-o') + 1])
argv = json.loads((base / 'link-command.json').read_text())['argv']
archive, = (arg for arg in argv if Path(arg).name == 'libpcsx2-hosted.a')
assert subprocess.check_output(['llvm-ar', 'p', archive, emitter_object.name]) == emitter_object.read_bytes()


def replace(source, previous, new):
    assert source.count(previous) == 1, previous
    return source.replace(previous, new)


s = emitter_source.read_text()
s = replace(s, '#include "ee-span-trace.hpp"', '#include "ee-span-trace.hpp"\n#include "ee-late-warm.hpp"')
s = replace(s, 'if (auto* const trace = iz_ps2_ee_span_trace; trace && !warm_batch) [[unlikely]]',
    'const auto trace = iz_ps2_ee_span_trace;\n'
    '\tconst auto learner = ps2::ee_warm::active_late_warm_capture;\n'
    '\tif ((trace || learner) && !warm_batch) [[unlikely]]')
s = replace(s, '\t\ttrace->Record(startpc, bytes, eligible, eligible ? ps2::ee_warm::Hash({scanned_block_bytes, bytes}) : 0);',
    '\t\tconst u64 hash = eligible ? ps2::ee_warm::Hash({scanned_block_bytes, bytes}) : 0;\n'
    '\t\tif (trace)\n\t\t\ttrace->Record(startpc, bytes, eligible, hash);\n'
    '\t\tif (learner)\n\t\t\tlearner->Observe(HWADDR(startpc), startpc, bytes, eligible, hash);')
(out / 'iR5900-arm64.cpp').write_text(s)
for name in ('Arm64EeIndexedBaseBlocks.h', 'Arm64EeIndexedBlockArray.h', 'EeIncomingPageBatchPrivate.inc'):
    if (emitter_source.parent / name).exists():
        shutil.copyfile(emitter_source.parent / name, out / name)

s = (base / 'ps2_vm_runner.cpp').read_text()
s = replace(s, 'EeCompileSpanTrace* iz_ps2_ee_span_trace = nullptr;',
    'EeCompileSpanTrace* iz_ps2_ee_span_trace = nullptr;\n'
    'ps2::ee_warm::LateWarmCapture* ps2::ee_warm::active_late_warm_capture = nullptr;')
s = replace(s, '#include "ee-span-trace.hpp"', '#include "ee-span-trace.hpp"\n#include "ee-late-warm.hpp"')
s = replace(s, '    const ScopedGuard clear_span_trace([] { iz_ps2_ee_span_trace = nullptr; });',
    '''    const ScopedGuard clear_span_trace([] { iz_ps2_ee_span_trace = nullptr; });
#ifdef IZPS2_SWITCH2_EE_WARM
    std::unique_ptr<Sb4EeLateWarm> late_warm;
    const ScopedGuard clear_late_warm([] { ps2::ee_warm::active_late_warm_capture = nullptr; });
#endif''')
s = replace(s, '#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)\n            if (hosted && neutral_requested && ee_warm.read_only_seed',
    '''#ifdef IZPS2_HOSTED_WORKER
            if (!ee_warm.embedded_entries.empty() && control)
                late_warm = start_sb4_ee_late_warm(ee_warm, *control);
#endif
#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)
            if (hosted && neutral_requested && ee_warm.read_only_seed''')
s = replace(s, '            s_frame_windows.Pause();',
    '''            s_frame_windows.Pause();
            if (ps2::ee_warm::active_late_warm_capture)
                ps2::ee_warm::active_late_warm_capture->Discontinuity();''')
s = replace(s, '    finish_sb4_ee_warm(ee_warm);',
    '''    ps2::ee_warm::active_late_warm_capture = nullptr;
    if (late_warm)
        finish_sb4_ee_late_warm(*late_warm);
    finish_sb4_ee_warm(ee_warm);''')
s = replace(s, '    const auto prepare_ee_warm = [&](const char* phase, bool allow_fastload = false) {',
    '''    const bool learn_late_requested =
#ifdef IZPS2_HOSTED_WORKER
        hosted && module_flag(*hosted, "ee-checkpoint-learn-late.flag");
#else
        false;
#endif
    const auto prepare_ee_warm = [&](const char* phase, bool allow_fastload = false) {''')
s = replace(s, 'extend_embedded_seed, warm_compile_burst, read_burst_extension, embedded_late_requested);',
    'extend_embedded_seed, warm_compile_burst, read_burst_extension, embedded_late_requested, learn_late_requested);')
(out / 'ps2_vm_runner.cpp').write_text(s)

s = (base / 'ps2_frame_windows.inc').read_text()
s = replace(s, '    if (iz_ps2_ee_span_trace) iz_ps2_ee_span_trace->SetField(frame);',
    '''    if (iz_ps2_ee_span_trace) iz_ps2_ee_span_trace->SetField(frame);
    if (auto* const capture = ps2::ee_warm::active_late_warm_capture) [[unlikely]]
        capture->OnField(frame, tick, recGetEeCompileAudit().ticks);''')
(out / 'ps2_frame_windows.inc').write_text(s)

s = (base / 'ps2_sb4_ee_warm.inc').read_text()
s = replace(s, '    bool read_only_seed = false;', '    bool read_only_seed = false;\n    std::vector<ps2::ee_warm::Entry> embedded_entries;')
s = replace(s, '        if (!result.valid)\n            profile.previous.clear();',
    '''        if (!result.valid)
            profile.previous.clear();
        const size_t expected = embedded_seed.empty() ? 0 :
            (embedded_seed.size() - ps2::ee_warm::HeaderBytes) / ps2::ee_warm::RecordBytes;
        if (!expected || !result.valid || result.stale ||
            result.usable != expected || attempted != expected || available != expected)
            profile.embedded_entries.clear();''')
s = replace(s, 'bool warm_compile_burst, bool read_burst_extension, bool embedded_late_requested)',
    'bool warm_compile_burst, bool read_burst_extension, bool embedded_late_requested, bool learn_late_requested)')
s = replace(s, '                        available += compiled;\n                        return compiled == entries.size();',
    '''                        available += compiled;
                        if (learn_late_requested && !embedded_seed.empty() && compiled == entries.size())
                            profile.embedded_entries.insert(profile.embedded_entries.end(), entries.begin(), entries.end());
                        return compiled == entries.size();''')
s = replace(s, '} // namespace\n', (out / 'late-warm-storage.inc').read_text() + '\n} // namespace\n')
(out / 'ps2_sb4_ee_warm.inc').write_text(s)
