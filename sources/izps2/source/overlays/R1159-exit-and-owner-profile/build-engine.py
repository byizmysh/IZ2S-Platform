"""Rebuild only the instrumentation units of the R1157 hosted IZPS2 app."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
import sys

out = Path(__file__).resolve().parent
base = out.parent / "R1157-checkpoint-warm-shutdown-trace"
vu1 = out.parent / "R1155-vu1-cpu-time-sampling"
r1122 = out.parent / "R1122-ee-loop-smc-epoch-charge"
r1128 = out.parent / "R1128-ee-page-epoch"
panel = out.parents[3].parents[2]
run23 = panel / "iz2s-platform-run23"
run25 = panel / "iz2s-platform-run25"

link = json.loads((base / "link-command.json").read_text())
recipes = dict(json.loads((base / "compile-recipes.json").read_text()))
recipes["microVU-arm64.cpp"] = dict(json.loads((vu1 / "compile-recipes.json").read_text()))["microVU-arm64.cpp"]
audio = next(job for job in json.loads((r1122 / "commands.json").read_text())
             if job["name"] == "compile-native_audio_stream.cpp")
recipes["native_audio_stream.cpp"] = [
    audio["argv"][0], "-DIZPS2_HOSTED_WORKER=1", "-iquote", str(r1122 / "candidate-source"),
    *(arg for arg in audio["argv"][1:] if not arg.startswith("-DIZPS2_SDK_DSP_WAKE")),
]

def run(args, logfile, cwd=link["cwd"]):
    with (out / logfile).open("wb") as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)

def compile_unit(name):
    revisions = {
        '-DIZPS2_BUILD_REVISION="R1157"': '-DIZPS2_BUILD_REVISION="R1159"',
        '-DIZ_JIT_BUILD_ID="run25-hosted-r1157-r1118-checkpoint-warm-shutdown-trace"':
            '-DIZ_JIT_BUILD_ID="run25-hosted-r1159-r1118-exit-owner-profile"',
        str(base): str(out),
    }
    args = [revisions.get(arg, arg) for arg in recipes[name]]
    args[args.index("-c") + 1] = str(out / name)
    target = out / (name + ".obj")
    args[args.index("-o") + 1] = str(target)
    for flag in ("-MF", "-MT"):
        if flag in args:
            args[args.index(flag) + 1] = str(target.with_suffix(".d"))
    cwd = audio["cwd"] if name == "native_audio_stream.cpp" else link["cwd"]
    run(args, "compile-" + name + ".log", cwd)
    return name, args

units = ("hosted-entry.c", "ps2_vm_runner.cpp", "VMManager.cpp", "microVU-arm64.cpp", "native_audio_stream.cpp")
with ThreadPoolExecutor(max_workers=len(units)) as pool:
    compiled = dict(pool.map(compile_unit, units))
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")

archive = out / "libpcsx2-hosted.a"
shutil.copyfile(base / archive.name, archive)
core_units = ("VMManager.cpp.obj", "microVU-arm64.cpp.obj")
run(["llvm-ar", "r", archive, *(out / name for name in core_units)], "archive.log")

names = ("engine.elf", "engine.ld", "engine.map", "hosted-entry.c.obj",
         "ps2_vm_runner.cpp.obj", archive.name)
mapping = {str(base / name): str(out / name) for name in names}
mapping[str(r1128 / "compiled/native_audio_stream.cpp.obj")] = str(out / "native_audio_stream.cpp.obj")
assert all(link["argv"].count(path) == 1 for path in mapping)
link["argv"] = [mapping.get(arg, arg) for arg in link["argv"]]
link["coreBaseline"] = str(base / archive.name)
link["coreReplacements"] = list(core_units)
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
     out / "input.tile", out / "output.tile", "IZPS2 R1159"], "tile.log")
run([sys.executable, out / "build-frontend.py"], "frontend.log")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1159.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
