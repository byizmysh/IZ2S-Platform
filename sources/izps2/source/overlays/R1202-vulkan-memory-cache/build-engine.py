"""Build the R1193 engine with the verified hosted Vulkan cache workaround."""
from collections import Counter
from pathlib import Path
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
base = out.parent / "R1193-checkpoint-stall-seed"
root = out.parents[3]

panel = out.parents[3].parents[2]
run23 = panel / "iz2s-platform-run23"
run25 = panel / "iz2s-platform-run25"
link = json.loads((base / "link-command.json").read_text())
recipes = json.loads((base / "compile-recipes.json").read_text())

def run(args, logfile, cwd=link["cwd"]):
    with (out / logfile).open("wb") as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


compile_names = ("hosted-entry.c", "ps2_vm_runner.cpp", "native_pad.c")
recipes = {name: (recipes[name], link["cwd"]) for name in compile_names if name in recipes}
pad_recipe = json.loads((root / "build/izps2-homebrew/logs/hen-izps2-r799-integration-3RdPdi/compile/native_pad.c.json").read_text())
recipes["native_pad.c"] = (pad_recipe["argv"], pad_recipe["cwd"])


def compile_unit(unit):
    name, (original, cwd) = unit
    args = [arg.replace("r1193-r1118-checkpoint-stall-seed", "r1202-r1118-vulkan-memory-cache")
            .replace('-DIZPS2_BUILD_REVISION="R1193"', '-DIZPS2_BUILD_REVISION="R1202"')
            .replace(f"-I{base}", f"-I{out}")
            for arg in original]
    args[args.index("-c") + 1] = str(out / name)
    target = str(out / (name + ".obj"))
    args[args.index("-o") + 1] = target
    for flag in ("-MT", "-MF"):
        if flag in args:
            args[args.index(flag) + 1] = target + ".d"
    run(args, "compile-" + name + ".log", cwd)
    return name, args


with ThreadPoolExecutor(4) as pool:
    compiled = dict(pool.map(compile_unit, recipes.items()))
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")
names = {"hosted-entry.c.obj", "ps2_vm_runner.cpp.obj", "native_pad.c.raw.o",
         "engine.ld", "engine.elf", "engine.map"}
mapping = {arg: str(out / Path(arg).name) for arg in link["argv"] if Path(arg).name in names}
assert Counter(Path(arg).name for arg in mapping) == Counter(dict.fromkeys(names, 1))
old_pad = next(arg for arg in mapping if Path(arg).name == "native_pad.c.raw.o")
mapping[old_pad] = str(out / "native_pad.c.obj")
link["argv"] = [mapping.get(arg, arg) for arg in link["argv"]]
(out / "link-command.json").write_text(json.dumps(link, indent=2) + "\n")
run(link["argv"], "link.log")

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
report.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
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
     out / "input.tile", out / "output.tile", "IZPS2 R1202"], "tile.log")
run([sys.executable, out / "build-frontend.py"], "frontend.log")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1202.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
