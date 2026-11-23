"""Build the active SPU2 voice routing candidate and package it for RUN25."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
base = out.parent / 'R1270-iop-host-hotspots'
r1118 = out.parent / 'R1118-ee-softmem-unaligned-pairs'
panel = out.parents[3].parents[2]
run23 = panel / 'iz2s-platform-run23'
run25 = panel / 'iz2s-platform-run25'
link = json.loads((base/'link-command.json').read_text())
recipes = json.loads((base/'compile-recipes.json').read_text())


def run(args, logfile, cwd=link['cwd']):
    with (out/logfile).open('wb') as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


def compile_unit(name):
    recipe = recipes[name]
    args = [arg.replace(str(base),str(out))
            .replace('r1270-r1175-spu2-mix-budget','r1271-r1175-spu2-active-routing')
            .replace('-DIZPS2_BUILD_REVISION="R1270"','-DIZPS2_BUILD_REVISION="R1271"')
            for arg in recipe['argv']]

    obj = name + '.obj'
    args[args.index('-o')+1] = str(out/obj)
    args[args.index('-c')+1]=str(out/name)
    for flag in ('-MF','-MT'):
        if flag in args:
            args[args.index(flag)+1]=str(out/(name+'.d'))
    run(args, 'compile-'+name+'.log', recipe['cwd'])
    return name, {'argv':args,'cwd':recipe['cwd']}


for name in ('hosted-entry.c','ps2_vm_runner.cpp','spu2-mix-timing.h','ps2_frame_windows.hpp','ps2_frame_windows.inc','checkpoint-extended-seed.h','checkpoint-burst-seed.h',
             'native_ee_warm_profile.hpp','native_ee_warm_retain.hpp','iop-slice-breakdown.h',
             'embedded-spirv.h','mtgs-command-trace.h','mtgs-submit-trace.h','engine.ld',
             'ee-span-trace.hpp','ee-affinity-native.inc','ee-affinity-trial.hpp',
             'portable-image.py','verify-relocation.c','input.tile','verify-app.cjs','seal-image.py',
             'checkpoint-late-suffix.h','checkpoint-late-suffix.bin','ps2_sb4_ee_warm.inc',
             'vif1-queue-publication.h','gs-field-profile.h','nfs-gs-live.h','nfs-gs-peaks.h',
             'ee-late-warm.hpp','ps2_mtvu_bulk_v4.hpp'):
    shutil.copyfile(base/name,out/name)
(out/'package-app.cjs').write_text((base/'package-app.cjs').read_text().replace('R1270','R1271'))
mixer, = [entry for entry in json.loads((r1118/'commands.json').read_text())
          if entry['name']=='compile-Mixer.cpp']
args = mixer['argv']
recipes['Mixer.cpp'] = {'argv':[args[0], '-I'+str(out), *args[1:]], 'cwd':mixer['cwd']}
with ThreadPoolExecutor(3) as pool:
    compiled = dict(pool.map(compile_unit, ('hosted-entry.c', 'ps2_vm_runner.cpp', 'Mixer.cpp')))

# Keep the SDK objcopy pass from R1153 and compare the active mixer to R1270.
restore_names = out / 'restore-vulkan-symbols.txt'
shutil.copyfile(base/'restore-vulkan-symbols.txt', restore_names)
objcopy = Path(link['argv'][4]).with_name('aarch64-none-elf-objcopy')
transform = [str(objcopy), '--redefine-syms=' + str(restore_names)]
anchor = base / 'Mixer.cpp.obj'
record = json.loads((base/'member-provenance.json').read_text())
assert sha256(anchor.read_bytes()).hexdigest() == record['candidateSha256']
run([*transform, out/'Mixer.cpp.obj'], 'archive-mixer-symbols.log')
compiled['Mixer.cpp']['postCompileArgv'] = [*transform, str(out/'Mixer.cpp.obj')]
for name, recipe in recipes.items():
    if name not in compiled:
        compiled[name] = {**recipe, 'inheritedObject': True}
(out / 'compile-recipes.json').write_text(json.dumps(compiled, indent=2) + '\n')

stock, = (Path(arg) for arg in link['argv'] if Path(arg).name == 'libpcsx2-hosted.a')
archive = out / stock.name
member = out/'Mixer.cpp.obj'
active = subprocess.check_output(['llvm-ar','p',stock,member.name])
assert active == anchor.read_bytes(), member.name
(out / 'member-provenance.json').write_text(json.dumps({
    'baselineArchive': str(stock), 'reproducedFrom': str(anchor),
    'baselineSha256': sha256(active).hexdigest(), 'candidateSha256': sha256(member.read_bytes()).hexdigest()
}, indent=2) + '\n')
shutil.copyfile(stock, archive)
run(['llvm-ar', 'rD', archive, member], 'archive.log')
assert subprocess.check_output(['llvm-ar','p',archive,member.name]) == member.read_bytes()
names = {'hosted-entry.c.obj','ps2_vm_runner.cpp.obj','engine.ld','engine.elf','engine.map', archive.name}
assert Counter(Path(arg).name for arg in link['argv'] if Path(arg).name in names) == Counter(names)
link['argv'] = [str(out/Path(arg).name) if Path(arg).name in names else arg for arg in link['argv']]
(out/'link-command.json').write_text(json.dumps(link,indent=2)+'\n')
run(link['argv'],'link.log')
run([sys.executable, out/'seal-image.py', out/'engine.elf'], 'seal.log')

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
run([sys.executable, out.parent / "R1122-ee-loop-smc-epoch-charge/render-tile.py", run25 / "language/fonts",
     out / "input.tile", out / "output.tile", "IZPS2 R1271"], "tile.log")
(out / "frontend").mkdir(exist_ok=True)
frontend = base / "frontend/frontend.bin"
shutil.copyfile(frontend, out / "frontend/frontend.bin")
(out / "frontend-reuse.json").write_text(json.dumps({
    "inherited": str(frontend), "sha256": sha256(frontend.read_bytes()).hexdigest()
}, indent=2) + "\n")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1271.iza"
(out / "SHA256SUMS").write_text(f"{sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
