"""Trace R1162 hosted shutdown ownership without changing release decisions."""
from collections import Counter
from pathlib import Path
import hashlib
import json
import shlex
from concurrent.futures import ThreadPoolExecutor
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
base = out.parent / "R1162-audio-process-affinity"

panel = out.parents[3].parents[2]
run23 = panel / "iz2s-platform-run23"
run25 = panel / "iz2s-platform-run25"
link = json.loads((base / "link-command.json").read_text())
recipes = json.loads((base / "compile-recipes.json").read_text())

def run(args, logfile, cwd=link["cwd"]):
    with (out / logfile).open("wb") as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


def recipe(revision, name):
    path = next(out.parent.glob(f"R{revision}-*")) / "compile-recipes.json"
    return dict(json.loads(path.read_text()))[name]


raw_commands = {Path(row["file"]).name: shlex.split(row["command"])
                for row in json.loads((Path(link["cwd"]) / "compile_commands.json").read_text())}
recipes = {
    "hosted-entry.c": recipes["hosted-entry.c"],
    "ps2_vm_runner.cpp": recipes["ps2_vm_runner.cpp"],
    "ps2_sb4_vm_entry.cpp": recipe(1157, "ps2_sb4_vm_entry.cpp"),
    "ps2_sb4_vm_fs.cpp": recipe(1146, "ps2_sb4_vm_fs.cpp"),
    "native_audio_stream.cpp": recipe(1159, "native_audio_stream.cpp"),
    "ps2_sb4_fs.c": raw_commands["ps2_sb4_fs.c"],
    "ps2_sb4_sd_saves.cpp": raw_commands["ps2_sb4_sd_saves.cpp"],
}


def compile_unit(unit):
    name, original = unit
    args = [arg.replace(str(base), str(out))
            .replace("R1162", "R1163")
            .replace("r1162-r1118-audio-process-affinity", "r1163-r1118-exit-resource-audit")
            for arg in original]
    args[args.index("-c") + 1] = str(out / name)
    args[args.index("-o") + 1] = str(out / (name + ".obj"))
    run(args, "compile-" + name + ".log")
    return name, args


with ThreadPoolExecutor(4) as pool:
    compiled = dict(pool.map(compile_unit, recipes.items()))
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")
names = {name + ".obj" for name in recipes} | {"engine.elf", "engine.ld", "engine.map"}
mapping = {arg: str(out / Path(arg).name) for arg in link["argv"] if Path(arg).name in names}
assert Counter(Path(arg).name for arg in mapping) == Counter(dict.fromkeys(names, 1))
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
     out / "input.tile", out / "output.tile", "IZPS2 R1163"], "tile.log")
run([sys.executable, out / "build-frontend.py"], "frontend.log")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1163.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
