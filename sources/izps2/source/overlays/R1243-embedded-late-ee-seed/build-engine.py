"""Build the RUN25 package with the optional embedded late checkpoint seed."""
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
base = out.parent / 'R1240-ee-vu1-core-separation'
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
            .replace('r1240-r1118-ee-vu1-core-separation','r1243-r1118-embedded-late-ee-seed')
            .replace('-DIZPS2_BUILD_REVISION="R1240"','-DIZPS2_BUILD_REVISION="R1243"')
            for arg in recipe['argv']]
    args[args.index('-o')+1]=str(out/(name+'.obj'))
    args[args.index('-c')+1]=str(out/name)
    for flag in ('-MF','-MT'):
        if flag in args:
            args[args.index(flag)+1]=str(out/(name+'.d'))
    run(args, 'compile-'+name+'.log', recipe['cwd'])
    return name, {'argv':args,'cwd':recipe['cwd']}


run([sys.executable,out/'prepare-sources.py'],'prepare.log')
for name in ('hosted-entry.c','checkpoint-extended-seed.h','checkpoint-burst-seed.h',
             'native_ee_warm_profile.hpp','native_ee_warm_retain.hpp','ps2_frame_windows.hpp','iop-slice-breakdown.h',
             'embedded-spirv.h','mtgs-command-trace.h','mtgs-submit-trace.h','engine.ld',
             'ps2_frame_windows.inc','ee-span-trace.hpp','ee-affinity-native.inc','ee-affinity-trial.hpp',
             'portable-image.py','verify-relocation.c','input.tile','verify-app.cjs'):
    shutil.copyfile(base/name,out/name)
(out/'package-app.cjs').write_text((base/'package-app.cjs').read_text().replace('R1240','R1243'))
with ThreadPoolExecutor(2) as pool:
    compiled=dict(pool.map(compile_unit,('hosted-entry.c','ps2_vm_runner.cpp')))
for name in ('iR5900-arm64.cpp','R5900.cpp','embedded-spirv.cpp','ee-affinity-service.c'):
    compiled[name] = {**recipes[name],'inheritedObject':True}
(out/'compile-recipes.json').write_text(json.dumps(compiled,indent=2)+'\n')
names = {'hosted-entry.c.obj','ps2_vm_runner.cpp.obj','engine.ld','engine.elf','engine.map'}
assert Counter(Path(arg).name for arg in link['argv'] if Path(arg).name in names) == Counter(names)
link['argv'] = [str(out/Path(arg).name) if Path(arg).name in names else arg for arg in link['argv']]
(out/'link-command.json').write_text(json.dumps(link,indent=2)+'\n')
run(link['argv'],'link.log')

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
     out / "input.tile", out / "output.tile", "IZPS2 R1243"], "tile.log")
(out / "frontend").mkdir(exist_ok=True)
frontend = base / "frontend/frontend.bin"
shutil.copyfile(frontend, out / "frontend/frontend.bin")
(out / "frontend-reuse.json").write_text(json.dumps({
    "inherited": str(frontend), "sha256": sha256(frontend.read_bytes()).hexdigest()
}, indent=2) + "\n")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1243.iza"
(out / "SHA256SUMS").write_text(f"{sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
