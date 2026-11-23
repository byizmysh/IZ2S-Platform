"""Build the combined RUN25 ZIP, checkpoint JIT and audio candidate."""
from collections import Counter
from pathlib import Path
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
import shutil
import shlex
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
base = out.parent / "R1165-live-file-owner"

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


def compiled_command(revision, name):
    path = next(out.parent.glob(f"R{revision}-*")) / "commands.json"
    return next(row["argv"] for row in json.loads(path.read_text()) if row["name"] == "compile-" + name)


core_entries = json.loads((Path(link["cwd"]) / "compile_commands.json").read_text())
backup = next(entry for entry in core_entries if entry["file"].endswith("/MemcardBackup.cpp"))
core_names = {"MemcardBackup.cpp", "Patch.cpp"}
recipes = {
    "hosted-entry.c": recipes["hosted-entry.c"],
    "ps2_vm_runner.cpp": recipes["ps2_vm_runner.cpp"],
    "HostSys.cpp": recipe(1164, "HostSys.cpp"),
    "native_audio_stream.cpp": recipe(1163, "native_audio_stream.cpp"),
    "audio-dsp-wake-sdk.c": compiled_command(1111, "audio-dsp-wake-sdk.c"),
    "MemcardBackup.cpp": shlex.split(backup["command"]),
    "Patch.cpp": compiled_command(1118, "Patch.cpp"),
}


def compile_unit(unit):
    name, original = unit
    args = list(original)
    if name in ("hosted-entry.c", "ps2_vm_runner.cpp"):
        args = [arg.replace(str(base), str(out)).replace("R1165", "R1166")
                .replace("r1165-r1118-live-file-owner", "r1166-r1118-exit-zip-jit-audio") for arg in args]
    if name == "HostSys.cpp":
        args[args.index("-iquote") + 1] = str(out)
        args[args.index("-MF") + 1] = str(out / "host-sys.d")
    if name in core_names:
        args[1:1] = ["-I" + str(out)]
    else:
        args[args.index("-c") + 1] = str(out / name)
    if name == "native_audio_stream.cpp":
        args[1:1] = ["-DIZPS2_SDK_DSP_WAKE=1"]
    if name == "audio-dsp-wake-sdk.c" and "-mbranch-protection=bti" not in args:
        args[1:1] = ["-mbranch-protection=bti"]
    args[args.index("-o") + 1] = str(out / (name + ".obj"))
    run(args, "compile-" + name + ".log")
    return name, args


with ThreadPoolExecutor(5) as pool:
    compiled = dict(pool.map(compile_unit, recipes.items()))
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")
core_archive = next(Path(arg) for arg in link["argv"] if arg.endswith("/libpcsx2-hosted.a"))
local_archive = out / core_archive.name
shutil.copyfile(core_archive, local_archive)
run(["llvm-ar", "r", local_archive, *(out / (name + ".obj") for name in sorted(core_names))], "archive.log")
names = {name + ".obj" for name in recipes.keys() - core_names - {"audio-dsp-wake-sdk.c"}}
names |= {"engine.elf", "engine.ld", "engine.map", core_archive.name}
mapping = {arg: str(out / Path(arg).name) for arg in link["argv"] if Path(arg).name in names}
assert Counter(Path(arg).name for arg in mapping) == Counter(dict.fromkeys(names, 1))
link["argv"] = [mapping.get(arg, arg) for arg in link["argv"]]
link["argv"].insert(link["argv"].index(str(out / "native_audio_stream.cpp.obj")) + 1,
                    str(out / "audio-dsp-wake-sdk.c.obj"))
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
     out / "input.tile", out / "output.tile", "IZPS2 R1166"], "tile.log")
run([sys.executable, out / "build-frontend.py"], "frontend.log")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1166.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
