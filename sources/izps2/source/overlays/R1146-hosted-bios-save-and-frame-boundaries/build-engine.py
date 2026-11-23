"""Relink the R1144 app with the hosted SD lifecycle fix."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
parent = out.parent / 'R1145-hosted-cache-storage'
runner_base = out.parent / 'R1144-ee-warm-after-first-vblank'
pinned = out.parent / 'R1122-ee-loop-smc-epoch-charge'
root = out.parents[3]
panel = root.parents[2]
run25 = panel / 'iz2s-platform-run25'
run23 = panel / 'iz2s-platform-run23'
release = 'R1146'
link = json.loads((parent / 'link-command.json').read_text())
cwd = Path(link['cwd'])

def run(argv, log):
    with (out / log).open('wb') as stream:
        subprocess.run(list(map(str, argv)), cwd=cwd, check=True,
                       stdout=stream, stderr=subprocess.STDOUT)

fs_name = 'ps2_sb4_vm_fs.cpp'
entry_name = 'hosted-entry.c'
runner_name = 'ps2_vm_runner.cpp'
card_name = 'MemcardBackup.cpp'
cdvd_name = 'CDVD.cpp'
raw_commands = {Path(row['file']).name: shlex.split(row['command'])
                for row in json.loads((cwd / 'compile_commands.json').read_text())}
entry = next(argv for name, argv in json.loads((parent / 'compile-recipes.json').read_text())
             if name == entry_name)
runner = next(argv for argv, _, name in json.loads((runner_base / 'compile-recipes.json').read_text())
              if name == runner_name)
runner = [str(out) if arg == str(runner_base) else arg.replace('R1144', release)
          if arg.startswith('-DIZPS2_BUILD_REVISION') else arg for arg in runner]
pinned_commands = json.loads((pinned / 'commands.json').read_text())
cdvd = next(row['argv'] for row in pinned_commands if Path(row['argv'][-1]).name == cdvd_name)
card = raw_commands[card_name]
for argv in [cdvd, card]:
    argv.insert(1, '-DIZPS2_HOSTED_WORKER=1')
units = [(fs_name, raw_commands[fs_name]), (entry_name, entry), (runner_name, runner),
         (card_name, card), (cdvd_name, cdvd)]
for name, argv in units:
    argv[argv.index('-c') + 1] = str(out / name)
    argv[argv.index('-o') + 1] = str(out / (name + '.obj'))
(out / 'compile-recipes.json').write_text(json.dumps(units, indent=2) + '\n')
with ThreadPoolExecutor(max_workers=4) as pool:
    for result in [pool.submit(run, argv, name + '.log') for name, argv in units]:
        result.result()

replacements = {'engine.elf', 'engine.ld', 'engine.map',
                fs_name + '.obj', entry_name + '.obj', runner_name + '.obj', 'libpcsx2-hosted.a'}
replaced = Counter()
for index, arg in enumerate(link['argv']):
    name = Path(arg).name
    if name in replacements:
        if name == 'libpcsx2-hosted.a':
            import shutil
            archive = out / name
            shutil.copyfile(arg, archive)
            run(['llvm-ar', 'r', archive, out / (cdvd_name + '.obj'), out / (card_name + '.obj')],
                'archive.log')
        link['argv'][index] = str(out / name)
        replaced[name] += 1
assert replaced == Counter(dict.fromkeys(replacements, 1)), replaced
link['platformReplacements'] = list(dict.fromkeys(
    link.get('platformReplacements', []) + [fs_name + '.obj']))
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
              originalEngineObjectRevision='R1131', output=str(engine))
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
