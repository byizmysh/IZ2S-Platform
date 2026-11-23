"""Enable the hardware-qualified suffix only in the existing exact checkpoint path."""

from pathlib import Path
import shutil

out = Path(__file__).resolve().parent
base = out.parent / 'R1243-embedded-late-ee-seed'
source = (base / 'ps2_vm_runner.cpp').read_text()
flag = 'hosted && module_flag(*hosted, "ee-checkpoint-embedded-late.flag");'
assert source.count(flag) == 1
source = source.replace(flag, 'hosted && !module_flag(*hosted, "ee-checkpoint-no-late-suffix.flag") &&\n        !module_flag(*hosted, "perf-ee-compile-spans.flag");')
(out / 'ps2_vm_runner.cpp').write_text(source)
shutil.copyfile(base / 'ps2_sb4_ee_warm.inc', out / 'ps2_sb4_ee_warm.inc')
shutil.copyfile(base / 'checkpoint-late-suffix.bin', out / 'checkpoint-late-suffix.bin')
header = (base / 'checkpoint-late-suffix.h').read_text().replace(str(base), str(out))
(out / 'checkpoint-late-suffix.h').write_text(header)
