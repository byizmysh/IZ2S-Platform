"""Use native reaping for the validated hosted IOP thread only."""
from collections import Counter
from pathlib import Path
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
import struct
import shutil
import subprocess
import sys

out = Path(__file__).resolve().parent
base = out.parent / "R1186-bounded-exit-ee-publication"
wake = out.parent / "R1185-iop-bounded-wake"

panel = out.parents[3].parents[2]
run23 = panel / "iz2s-platform-run23"
run25 = panel / "iz2s-platform-run25"
link = json.loads((base / "link-command.json").read_text())
recipes = json.loads((base / "compile-recipes.json").read_text())

def run(args, logfile, cwd=link["cwd"]):
    with (out / logfile).open("wb") as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


compile_names = ("hosted-entry.c", "ps2_vm_runner.cpp")
archive_names = ("IopThread.cpp", "ps2_iop_jit_owner.cpp")
wake_recipes = json.loads((wake / "compile-recipes.json").read_text())
recipes = {name: recipes[name] for name in compile_names}
recipes.update({name: wake_recipes[name] for name in archive_names})
sdk = run23 / "engines/izhen/sdk"
host_sdk = sdk / "sb4"
libounce = sdk / "devkitpro/libounce"
gcc = sdk / "devkitpro/devkitA64/bin/aarch64-none-elf-gcc"
host_recipe = [str(gcc), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-Wno-unused-parameter", "-mcpu=cortex-a78c+crypto", "-mtp=soft", "-fPIE",
    "-ffunction-sections", "-fdata-sections", "-D__SWITCH__", "-D__SWITCH2__",
    "-I" + str(host_sdk / "include"), "-I" + str(host_sdk / "source"),
    "-isystem", str(libounce / "include"), "-c", str(out / "host-posix.c"),
    "-o", str(out / "host-posix.o")]

def compile_unit(unit):
    name, original = unit
    args = [arg.replace(str(base), str(out)).replace(str(wake), str(out))
            .replace("r1186-r1118-bounded-exit-ee-publication", "r1187-r1118-iop-native-reap")
            .replace('-DIZPS2_BUILD_REVISION="R1185"', '-DIZPS2_BUILD_REVISION="R1187"')
            for arg in original]
    args[args.index("-c") + 1] = str(out / name)
    target = str(out / (name + ".obj"))
    args[args.index("-o") + 1] = target
    for flag in ("-MT", "-MF"):
        if flag in args:
            args[args.index(flag) + 1] = target + ".d"
    run(args, "compile-" + name + ".log")
    return name, args


with ThreadPoolExecutor(4) as pool:
    compiled_host = pool.submit(run, host_recipe, "compile-host-posix.c.log")
    compiled = dict(pool.map(compile_unit, recipes.items()))
    compiled_host.result()
compiled["host-posix.c"] = host_recipe
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")
archive = next(Path(arg) for arg in link["argv"] if arg.endswith("/libpcsx2-hosted.a"))
shutil.copyfile(archive, out / archive.name)
run(["llvm-ar", "r", out / archive.name, *(out / (name + ".obj") for name in archive_names)], "archive.log")
host_archive = next(Path(arg) for arg in link["argv"] if arg.endswith("/libsb4-host-posix.a"))
shutil.copyfile(host_archive, out / host_archive.name)
run(["llvm-ar", "r", out / host_archive.name, out / "host-posix.o"], "archive-host-posix.log")
names = {name + ".obj" for name in compile_names}
names.add(host_archive.name)
names |= {"engine.ld", "engine.elf", "engine.map", archive.name}
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
     out / "input.tile", out / "output.tile", "IZPS2 R1187"], "tile.log")
run([sys.executable, out / "build-frontend.py"], "frontend.log")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1187.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
