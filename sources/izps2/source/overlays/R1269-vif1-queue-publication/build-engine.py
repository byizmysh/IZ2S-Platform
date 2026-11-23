"""Build the RUN25 candidate for batched VIF1 queue publication and package it as one IZA."""
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
base = out.parent / 'R1268-threaded-iop-idle'
mtvu = out.parent / 'R1208-mtvu-v4-paired-rows'
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
            .replace('r1268-r1175-threaded-iop-idle','r1269-r1175-vif1-queue-publication')
            .replace('-DIZPS2_BUILD_REVISION="R1268"','-DIZPS2_BUILD_REVISION="R1269"')
            for arg in recipe['argv']]

    obj = name + '.obj'
    args[args.index('-o')+1] = str(out/obj)
    args[args.index('-c')+1]=str(out/name)
    for flag in ('-MF','-MT'):
        if flag in args:
            args[args.index(flag)+1]=str(out/(name+'.d'))
    run(args, 'compile-'+name+'.log', recipe['cwd'])
    return name, {'argv':args,'cwd':recipe['cwd']}


for name in ('hosted-entry.c','checkpoint-extended-seed.h','checkpoint-burst-seed.h',
             'native_ee_warm_profile.hpp','native_ee_warm_retain.hpp','ps2_frame_windows.hpp','iop-slice-breakdown.h',
             'embedded-spirv.h','mtgs-command-trace.h','mtgs-submit-trace.h','engine.ld',
             'ee-span-trace.hpp','ee-affinity-native.inc','ee-affinity-trial.hpp',
             'portable-image.py','verify-relocation.c','input.tile','verify-app.cjs','seal-image.py',
             'checkpoint-late-suffix.h','checkpoint-late-suffix.bin','ps2_sb4_ee_warm.inc'):
    shutil.copyfile(base/name,out/name)
(out/'package-app.cjs').write_text((base/'package-app.cjs').read_text().replace('R1268','R1269'))
mtvu_args = json.loads((mtvu/'compile-recipes.json').read_text())['MTVU.cpp']
vif, = [entry for entry in json.loads((r1118/'commands.json').read_text())
        if entry['name']=='compile-Vif_Transfer.cpp']
vif['argv'][1:1] = ['-iquote', str(r1118/'candidate-source')]
for name, args, cwd in (('MTVU.cpp', mtvu_args, str(mtvu)), ('Vif_Transfer.cpp', vif['argv'], vif['cwd'])):
    recipes[name] = {'argv':[args[0],'-I'+str(out),*args[1:]],'cwd':cwd}
with ThreadPoolExecutor(4) as pool:
    compiled = dict(pool.map(compile_unit, ('hosted-entry.c', 'ps2_vm_runner.cpp', 'MTVU.cpp', 'Vif_Transfer.cpp')))

# R1153 restored global Vulkan names across the inherited core archive with
# the SDK objcopy, which also rewrote the layout of this member's ELF sections.
symbol_map = out.parents[3] / 'build/izps2-homebrew/logs/hen-izps2-static-HSkTmn/vulkan-symbols.txt'
restore_names = out / 'restore-vulkan-symbols.txt'
restore_names.write_text(''.join(f'{scoped} {name}\n' for name, scoped in
                                 (line.split() for line in symbol_map.read_text().splitlines())))
objcopy = Path(link['argv'][4]).with_name('aarch64-none-elf-objcopy')
vif_transform = [str(objcopy), '--redefine-syms=' + str(restore_names)]
vif_anchor = out / 'provenance-Vif_Transfer.cpp.obj'
run([*vif_transform, r1118/'objects/Vif_Transfer.cpp.obj', vif_anchor], 'archive-vif-provenance.log')
run([*vif_transform, out/'Vif_Transfer.cpp.obj'], 'archive-vif-symbols.log')
compiled['Vif_Transfer.cpp']['postCompileArgv'] = [*vif_transform, str(out/'Vif_Transfer.cpp.obj')]
for name, recipe in recipes.items():
    if name not in compiled:
        compiled[name] = {**recipe, 'inheritedObject': True}
(out / 'compile-recipes.json').write_text(json.dumps(compiled, indent=2) + '\n')

stock, = (Path(arg) for arg in link['argv'] if Path(arg).name == 'libpcsx2-hosted.a')
archive = out / stock.name
members = [out/'MTVU.cpp.obj',out/'Vif_Transfer.cpp.obj']
provenance = {}
for member, anchor in zip(members, (mtvu/'MTVU.cpp.obj', vif_anchor)):
    active = subprocess.check_output(['llvm-ar','p',stock,member.name])
    assert active == anchor.read_bytes(), member.name
    provenance[member.name] = {'baselineArchive': str(stock), 'reproducedFrom': str(anchor),
                               'baselineSha256': sha256(active).hexdigest(),
                               'candidateSha256': sha256(member.read_bytes()).hexdigest()}
(out / 'member-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
shutil.copyfile(stock, archive)
run(['llvm-ar', 'rD', archive, *members], 'archive.log')
for member in members:
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
     out / "input.tile", out / "output.tile", "IZPS2 R1269"], "tile.log")
(out / "frontend").mkdir(exist_ok=True)
frontend = base / "frontend/frontend.bin"
shutil.copyfile(frontend, out / "frontend/frontend.bin")
(out / "frontend-reuse.json").write_text(json.dumps({
    "inherited": str(frontend), "sha256": sha256(frontend.read_bytes()).hexdigest()
}, indent=2) + "\n")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1269.iza"
(out / "SHA256SUMS").write_text(f"{sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
