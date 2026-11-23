"""Rebuild the verified RUN25 package with a longer, checkpoint-only EE seed."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from elftools.elf.elffile import ELFFile
from hashlib import sha256
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
series = out.parent
base = series / 'R1231-gta-embedded-spirv'
seed_model = series / 'R1230-ee-field5170-warm/model'
validation = json.loads((series / 'R1233-ee-warm-headroom/model/comparison.json').read_text())
assert validation['guestStateMatched'] and validation['targetFieldCompiles']['after'] == 2
panel = out.parents[3].parents[2]
run23 = panel / 'iz2s-platform-run23'
run25 = panel / 'iz2s-platform-run25'
link = json.loads((base / 'link-command.json').read_text())
recipes = json.loads((base / 'compile-recipes.json').read_text())


def run(args, logfile, cwd=link['cwd']):
    with (out / logfile).open('wb') as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


def substitute(body, old, new):
    assert body.count(old) == 1, old
    return body.replace(old, new)


short_seed = series / 'R1228-ee-warm-startup-bursts/model/extended-seed.profile'
full_seed = seed_model / 'extended-seed.profile'
short_data, full_data = (path.read_bytes() for path in (short_seed, full_seed))
for blob, count in ((short_data, 19270), (full_data, 20666)):
    assert struct.unpack_from('<8sIIII', blob) == (b'IZ2SEEW2', 0xb440a8fe, 0xb440a8fe, count, 0)
    assert len(blob) == 24 + count * 16
assert short_data[24:] == full_data[24:len(short_data)]
(out / 'checkpoint-burst-seed.profile').write_bytes(full_data)
header = (base / 'checkpoint-extended-seed.h').read_text()
full_header = (header.replace('izps2_extended_stall_seed','izps2_checkpoint_burst_seed')
               .replace(str(short_seed),str(out/'checkpoint-burst-seed.profile'))
               .replace(f'[{len(short_data)}]',f'[{len(full_data)}]'))
assert f'.incbin "{out}/checkpoint-burst-seed.profile"' in full_header
(out / 'checkpoint-burst-seed.h').write_text(full_header)
source = (base / 'native_ee_warm_profile.hpp').read_text()
(out / 'native_ee_warm_profile.hpp').write_text(substitute(source,'CheckpointMaxEntries = 20480;',
                                                            'CheckpointMaxEntries = 21504;'))
for name in ('hosted-entry.c','checkpoint-extended-seed.h','native_ee_warm_retain.hpp',
             'ps2_frame_windows.inc','embedded-spirv.h','mtgs-command-trace.h','mtgs-submit-trace.h','engine.ld',
             'portable-image.py','verify-relocation.c','input.tile','verify-app.cjs'):
    shutil.copyfile(base / name, out / name)

source = (base / 'ps2_sb4_ee_warm.inc').read_text()
source = substitute(source, '#include "checkpoint-extended-seed.h"',
                    '#include "checkpoint-extended-seed.h"\n#include "checkpoint-burst-seed.h"')
source = substitute(source, 'bool allow_isolated_mtvu, bool prefer_stall_seed, bool extend_embedded_seed)',
                    'bool allow_isolated_mtvu, bool prefer_stall_seed, bool extend_embedded_seed,\n'
                    '    bool warm_compile_burst)')
old_selection = '''            embedded_seed = extend_embedded_seed ? std::span<const u8>(izps2_extended_stall_seed)
                                                  : std::span<const u8>(izps2_stall_seed);'''
new_selection = '''            if (!extend_embedded_seed)
                embedded_seed = izps2_stall_seed;
            else if (warm_compile_burst)
                embedded_seed = izps2_checkpoint_burst_seed;
            else
                embedded_seed = izps2_extended_stall_seed;'''
source = substitute(source, old_selection, new_selection)
source = substitute(source, 'constexpr double EeWarmCheckpointBudgetMs = 1150.0;',
                    'constexpr double EeWarmCheckpointBudgetMs = 1150.0;\n'
                    'constexpr double EeWarmCheckpointBurstBudgetMs = 1400.0;')
source = substitute(source, '        budget_ms = EeWarmCheckpointBudgetMs;',
                    '        budget_ms = embedded_seed.size() == sizeof(izps2_checkpoint_burst_seed) ?\n'
                    '            EeWarmCheckpointBurstBudgetMs : EeWarmCheckpointBudgetMs;')
(out / 'ps2_sb4_ee_warm.inc').write_text(source)
source = (base / 'ps2_vm_runner.cpp').read_text()
source = substitute(source, '    const auto prepare_ee_warm =', '''    const bool warm_compile_burst =
#ifdef IZPS2_HOSTED_WORKER
        hosted && !module_flag(*hosted, "ee-checkpoint-base-prefix.flag");
#else
        false;
#endif
    const auto prepare_ee_warm =''')
source = substitute(source, '                                       extend_embedded_seed);',
                    '                                       extend_embedded_seed, warm_compile_burst);')
(out / 'ps2_vm_runner.cpp').write_text(source)


def compile_unit(name):
    recipe = recipes[name]
    args = [arg.replace(str(base),str(out)).replace('r1231-r1118-gta-embedded-spirv',
                                                  'r1235-r1118-gta-checkpoint-compile-burst')
            .replace('-DIZPS2_BUILD_REVISION="R1231"','-DIZPS2_BUILD_REVISION="R1235"')
            for arg in recipe['argv']]
    run(args, 'compile-' + name + '.log', recipe['cwd'])
    return name, {'argv':args,'cwd':recipe['cwd']}


with ThreadPoolExecutor(2) as pool:
    compiled = dict(pool.map(compile_unit, ('hosted-entry.c','ps2_vm_runner.cpp')))
compiled['embedded-spirv.cpp'] = {**recipes['embedded-spirv.cpp'],'inheritedObject':True}
(out / 'compile-recipes.json').write_text(json.dumps(compiled,indent=2)+'\n')
local_names = {'hosted-entry.c.obj','ps2_vm_runner.cpp.obj','engine.ld','engine.elf','engine.map'}
previous_link = link['argv']
assert Counter(Path(arg).name for arg in previous_link if Path(arg).name in local_names) == Counter(local_names)
link['argv'] = [str(out / Path(arg).name) if Path(arg).name in local_names else arg for arg in previous_link]
(out / 'link-command.json').write_text(json.dumps(link,indent=2)+'\n')
run(link['argv'], 'link.log')


def check_object(elf, name, expected):
    symbol, = elf.get_section_by_name('.symtab').get_symbol_by_name(name)
    assert symbol['st_size'] == len(expected)
    assert elf.get_section(symbol['st_shndx'])['sh_flags'] & 1 == 0
    offset, = elf.address_offsets(symbol['st_value'],symbol['st_size'])
    elf.stream.seek(offset)
    assert elf.stream.read(len(expected)) == expected
    return {'bytes':len(expected),'sha256':sha256(expected).hexdigest()}


with (out / 'engine.elf').open('rb') as stream:
    elf = ELFFile(stream)
    evidence = {
        'baseSeed':check_object(elf,'izps2_extended_stall_seed',short_data),
        'burstSeed':check_object(elf,'izps2_checkpoint_burst_seed',full_data),
        'inheritedShaders':check_object(elf,'izps2_spirv_words',(base/'spirv-shader-words.bin').read_bytes()),
        'unchangedArchives':[arg for arg in previous_link if arg.endswith('.a')],
    }
(out / 'linked-seeds.json').write_text(json.dumps(evidence,indent=2)+'\n')

# Reuse the published RUN25 container format and unchanged frontend.
(out / 'package-app.cjs').write_text((base / 'package-app.cjs').read_text().replace('R1231','R1235'))
packed = out / "engine-linked.izdata"
engine = out / "engine.izdata"
run([sys.executable, run23 / "tools/sd-launcher/relocatable.py", "--elf", out / "engine.elf",
     "--output", packed], "export.log")
run([sys.executable, out / "portable-image.py", packed, engine], "portable.log")
report = json.loads(packed.with_suffix(".izdata.json").read_text())
data = engine.read_bytes()
records_offset, count, stride = struct.unpack_from("<Q2I", data, 72)
kinds = Counter(struct.unpack_from("<H", data, records_offset + index * stride + 4)[0]
                for index in range(count))
report.update(bytes=len(data), sha256=sha256(data).hexdigest(),
              fileBytes=struct.unpack_from("<2I", data, 40),
              memoryBytes=struct.unpack_from("<2I", data, 48), records=count,
              recordKinds=kinds, linkedBytesPreserved=False, addressIndependent=True,
              originalEngineObjectRevision="R1118", output=str(engine))
engine.with_suffix(".izdata.json").write_text(json.dumps(report, indent=2) + "\n")

reloc = run25 / "core/console/sd-launcher/relocatable"
run(["cc", "-O2", "-I", reloc, out / "verify-relocation.c", reloc / "rebase.c", "-lz", "-o",
     out / "verify-relocation"], "verify-relocation-build.log")
with (out / "relocation-verification.jsonl").open("wb") as log:
    for megabytes in (32, -32, 128, -64):
        log.write(subprocess.check_output([out / "verify-relocation", engine, str(megabytes)]))
run([sys.executable, out.parent / "R1121-ee-loop-smc-epoch/render-tile.py", run25 / "language/fonts",
     out / "input.tile", out / "output.tile", "IZPS2 R1235"], "tile.log")
(out / "frontend").mkdir(exist_ok=True)
frontend = base / "frontend/frontend.bin"
shutil.copyfile(frontend, out / "frontend/frontend.bin")
(out / "frontend-reuse.json").write_text(json.dumps({
    "inherited": str(frontend), "sha256": sha256(frontend.read_bytes()).hexdigest()
}, indent=2) + "\n")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1235.iza"
(out / "SHA256SUMS").write_text(f"{sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
