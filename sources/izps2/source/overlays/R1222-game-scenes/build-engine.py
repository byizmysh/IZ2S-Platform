"""Build the R1221 engine with per-game save scenes in the pause menu."""
from collections import Counter
from pathlib import Path
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
base = out.parent / "R1221-ee-deferred-incoming"
root = out.parents[3]

panel = out.parents[3].parents[2]
run23 = panel / "iz2s-platform-run23"
run25 = panel / "iz2s-platform-run25"
link = json.loads((base / "link-command.json").read_text())
recipes = json.loads((base / "compile-recipes.json").read_text())

def run(args, logfile, cwd=link["cwd"]):
    with (out / logfile).open("wb") as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


compile_names = ("hosted-entry.c", "ps2_vm_runner.cpp")
recipes = {name: (recipes[name], link["cwd"]) for name in compile_names}
ui_source = out.parent / "R1122-ee-loop-smc-epoch-charge"
ui_recipe, = (command for command in json.loads((ui_source / "commands.json").read_text())
              if command["name"] == "compile-ps2_psx_ui.c")
recipes["ps2_psx_ui.c"] = (ui_recipe["argv"], ui_recipe["cwd"])

targets = {
    "hosted-entry.c": "hosted-entry.c.obj",
    "ps2_vm_runner.cpp": "ps2_vm_runner.cpp.obj",
    "ps2_psx_ui.c": "ps2_psx_ui.c.raw.o",
}

def compile_unit(unit):
    name, (original, cwd) = unit
    args = [arg.replace("r1221-r1118-ee-deferred-incoming", "r1222-r1118-game-scenes")
            .replace('-DIZPS2_BUILD_REVISION="R1221"', '-DIZPS2_BUILD_REVISION="R1222"')
            for arg in original]
    args[1:1] = ["-iquote", str(out / "frontend-source"), "-iquote", str(base)]
    source = out / name if name != "ps2_psx_ui.c" else out / "frontend-source" / name
    args[args.index("-c") + 1] = str(source)
    target = str(out / targets[name])
    args[args.index("-o") + 1] = target
    for flag in ("-MT", "-MF"):
        if flag in args:
            args[args.index(flag) + 1] = target + ".d"
    run(args, "compile-" + name + ".log", cwd)
    return name, {"argv": args, "cwd": cwd}

with ThreadPoolExecutor(3) as pool:
    compiled = dict(pool.map(compile_unit, recipes.items()))
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")
names = {*targets.values(), "engine.ld", "engine.elf", "engine.map"}
mapping = {arg: str(out / Path(arg).name) for arg in link["argv"] if Path(arg).name in names}
assert Counter(Path(arg).name for arg in mapping) == Counter(dict.fromkeys(names, 1))
link["argv"] = [mapping.get(arg, arg) for arg in link["argv"]]
screenshot_wrap = "--wrap=_Z24SaveState_SaveScreenshotv"
assert screenshot_wrap not in link["argv"]
linker_options = link["argv"].index("-Xlinker")
link["argv"][linker_options:linker_options] = ["-Xlinker", screenshot_wrap]
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
     out / "input.tile", out / "output.tile", "IZPS2 R1222"], "tile.log")
run([sys.executable, out / "build-frontend.py"], "frontend.log")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1222.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
