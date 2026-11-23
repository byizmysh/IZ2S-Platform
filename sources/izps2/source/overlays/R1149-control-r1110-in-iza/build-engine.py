"""Package the verified R1110 emulator archive with the R1146 hosted app bridge."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
parent = out.parent / 'R1146-hosted-bios-save-and-frame-boundaries'
root = out.parents[3]
panel = root.parents[2]
run25 = panel / 'iz2s-platform-run25'
run23 = panel / 'iz2s-platform-run23'
release = 'R1149'
link = json.loads((parent / 'link-command.json').read_text())
cwd = Path(link['cwd'])

def run(argv, log):
    with (out / log).open('wb') as stream:
        subprocess.run(list(map(str, argv)), cwd=cwd, check=True,
                       stdout=stream, stderr=subprocess.STDOUT)

old = out.parent / 'R1110-vu1-deferred-guarded-windows/objects'
hosted = out.parent / 'R1128-ee-page-epoch/compiled'
source = out / 'hosted-entry.c'
entry = next(argv for name, argv in json.loads((parent / 'compile-recipes.json').read_text())
             if name == source.name)
entry[entry.index('-c') + 1] = str(source)
entry[entry.index('-o') + 1] = str(source.with_suffix('.c.obj'))
run(entry, 'hosted-entry.log')
name = 'ps2_vm_runner.cpp'
runner = next(argv for source_name, argv in json.loads((parent / 'compile-recipes.json').read_text())
              if source_name == name)
runner = [str(out) if arg == str(parent) else arg for arg in runner
          if not arg.startswith('-DIZPS2_SWITCH2_EAGER_AB')]
for index, arg in enumerate(runner):
    if arg.startswith('-DIZPS2_BUILD_REVISION='):
        runner[index] = '-DIZPS2_BUILD_REVISION="R1149"'
    if arg.startswith('-DIZ_JIT_BUILD_ID='):
        runner[index] = '-DIZ_JIT_BUILD_ID="run25-hosted-r1149-r1110-control"'
runner[runner.index('-c') + 1] = str(out / name)
runner[runner.index('-o') + 1] = str(out / (name + '.obj'))
run(runner, 'runner.log')
# The old launcher allowed a single 128 MiB GS ring in a larger global BSS.
# The relocatable worker has a 128 MiB limit for its entire writable segment.
old_commands = json.loads((old.parent / 'commands.json').read_text())
gs = next(row['argv'] for row in old_commands if row['name'] == 'compile-MTGS.cpp')
gs = ['-DIZPS2_SWITCH2_WIDE_MTGS=8' if arg == '-DIZPS2_SWITCH2_WIDE_MTGS=16' else arg
      for arg in gs]
assert gs.count('-DIZPS2_SWITCH2_WIDE_MTGS=8') == 1
gs[gs.index('-o') + 1] = str(out / 'MTGS.cpp.obj')
run(gs, 'mtgs.log')

(out / 'compile-recipes.json').write_text(json.dumps([(source.name, entry), (name, runner), ('MTGS.cpp', gs)], indent=2) + '\n')
replacements = {'engine.elf', 'engine.ld', 'engine.map', 'hosted-entry.c.obj', 'ps2_vm_runner.cpp.obj', 'libpcsx2-hosted.a'}
replaced = Counter()
for index, arg in enumerate(link['argv']):
    name = Path(arg).name
    if name in replacements:
        if name == 'libpcsx2-hosted.a':
            archive = out / name
            symbol_map = root / 'build/izps2-homebrew/logs/hen-izps2-static-HSkTmn/vulkan-symbols.txt'
            restore_names = out / 'restore-vulkan-symbols.txt'
            restore_names.write_text(''.join(f'{scoped} {name}\n' for name, scoped in
                                             (line.split() for line in symbol_map.read_text().splitlines())))
            tool = Path(link['argv'][4]).with_name('aarch64-none-elf-objcopy')
            run([tool, '--redefine-syms=' + str(restore_names), old / 'libpcsx2-ee-block.a', archive],
                'archive-vulkan-symbols.log')
            bridge_objects = ['Memory.cpp.obj', 'FlatFileReader.cpp.obj']
            run(['llvm-ar', 'r', archive, *(hosted / member for member in bridge_objects),
                 parent / 'CDVD.cpp.obj', parent / 'MemcardBackup.cpp.obj', out / 'MTGS.cpp.obj'], 'archive.log')
        link['argv'][index] = str(out / name)
        replaced[name] += 1
assert replaced == Counter(dict.fromkeys(replacements, 1)), replaced
link['coreBaseline'] = str(old / 'libpcsx2-ee-block.a')
link['coreReplacements'] = ['Memory.cpp.obj', 'FlatFileReader.cpp.obj', 'CDVD.cpp.obj', 'MemcardBackup.cpp.obj', 'MTGS.cpp.obj']
(out / 'link-command.json').write_text(json.dumps(link, indent=2) + '\n')
run(link['argv'], 'link.log')

packed = out / 'engine-linked.izdata'
engine = out / 'engine.izdata'
run([sys.executable, run23 / 'tools/sd-launcher/relocatable.py',
     '--elf', out / 'engine.elf', '--output', packed], 'export.log')
run([sys.executable, out / 'portable-image.py', packed, engine], 'portable.log')
report = json.loads(packed.with_suffix('.izdata.json').read_text())
data = engine.read_bytes()
file_bytes = struct.unpack_from('<2I', data, 40)
memory_bytes = struct.unpack_from('<2I', data, 48)
records_offset, count, stride = struct.unpack_from('<Q2I', data, 72)
records = Counter(struct.unpack_from('<H', data, records_offset + index * stride + 4)[0]
                  for index in range(count))
report.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), fileBytes=file_bytes,
              memoryBytes=memory_bytes, records=count, recordKinds=records,
              linkedBytesPreserved=False, addressIndependent=True,
              originalEngineObjectRevision='R1110', output=str(engine))
engine.with_suffix('.izdata.json').write_text(json.dumps(report, indent=2) + '\n')

reloc = run25 / 'core/console/sd-launcher/relocatable'
run(['cc', '-O2', '-I', reloc, out / 'verify-relocation.c', reloc / 'rebase.c',
     '-lz', '-o', out / 'verify-relocation'], 'verify-relocation-build.log')
verification = [subprocess.check_output([out / 'verify-relocation', engine, str(distance)])
                for distance in [32, -32, 128, -64]]
(out / 'relocation-verification.jsonl').write_bytes(b''.join(verification))

run([sys.executable, out.parent / 'R1121-ee-loop-smc-epoch/render-tile.py', run25 / 'language/fonts',
     out / 'input.tile', out / 'output.tile', 'IZPS2 ' + release], 'tile.log')
run([sys.executable, out / 'build-frontend.py'], 'frontend.log')
run(['node', out / 'package-app.cjs'], 'package.log')
run(['node', out / 'verify-app.cjs'], 'verify-app.log')
