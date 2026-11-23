"""Apply one optional host scheduling change to the verified R1238 runner."""
from pathlib import Path

out = Path(__file__).resolve().parent
base = out.parent/'R1238-native-ee-spans'
source = (base/'ps2_vm_runner.cpp').read_text()


def change(old,new):
    global source
    assert source.count(old)==1,old
    source=source.replace(old,new)


change('#include "ps2_mtvu_jit.h"',
       '#include "ps2_mtvu_jit.h"\n#ifdef IZPS2_HOSTED_WORKER\n#include "ee-affinity-native.inc"\n#endif')
change('    bool first_dispatch = control != nullptr;', '''#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)
    EeAffinityTrial<EeAffinityPlatform> affinity;
    if (hosted && s_neutral_replay.state == Ps2NeutralReplay::State::Running &&
        module_flag(*hosted, "perf-ee-vu1-affinity.flag"))
    {
        const bool restrict_mask = module_flag(*hosted, "ee-exclude-vu1-core.flag");
        const auto worker = iz_ps2_vu1_cpu_ticks();
        affinity.Start(restrict_mask, worker.handle);
        Console.WriteLn("IZPS2_EE_AFFINITY requested=%u active=%u ee_mask=%llx vu1_mask=%llx selected=%llx "
                        "ee_query=0x%x vu_query=0x%x set=0x%x verify=0x%x",
            unsigned(restrict_mask), unsigned(affinity.verified),
            static_cast<unsigned long long>(affinity.saved.mask),
            static_cast<unsigned long long>(affinity.vu.mask),
            static_cast<unsigned long long>(affinity.selected.mask),
            affinity.ee_query, affinity.vu_query, affinity.set_result, affinity.verify_result);
    }
#endif
    bool first_dispatch = control != nullptr;''')
change('    trace_exit("26 VM shutdown begin\\n");', '''#if defined(IZPS2_HOSTED_WORKER) && defined(IZPS2_SWITCH2_SESSION_TIMING)
    affinity.Restore();
    if (affinity.restore_attempted)
        Console.WriteLn("IZPS2_EE_AFFINITY_RESTORE result=0x%x restored=%u",
            affinity.restore_result, unsigned(!affinity.changed));
#endif
    trace_exit("26 VM shutdown begin\\n");''')
(out/'ps2_vm_runner.cpp').write_text(source)
