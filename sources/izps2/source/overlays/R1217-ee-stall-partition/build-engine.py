"""Build the R1216 hosted engine with low-overhead opt-in EE gap attribution."""
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
base = out.parent / "R1216-vif-gif-traffic"
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
dma = out.parent / "R1206-ee-dma-attribution"
dma_recipe = json.loads((dma / "compile-recipes.json").read_text())["R5900.cpp"]
recipes["R5900.cpp"] = (dma_recipe, link["cwd"])
r1043 = out.parent / "R1043-prelinked-menu"
counter_recipe = next(rec for rec in json.loads((r1043/"commands.json").read_text())
                      if rec["name"] == "compile-Counters.cpp")
recipes["Counters.cpp"] = (counter_recipe["argv"], counter_recipe["cwd"])


def compile_unit(unit):
    name, (original, cwd) = unit
    args = [arg.replace("r1216-r1118-ee-block-index-leaves", "r1217-r1118-ee-stall-partition")
            .replace('-DIZPS2_BUILD_REVISION="R1216"', '-DIZPS2_BUILD_REVISION="R1217"')
            .replace(f"-I{base}", f"-I{out}")
            for arg in original]
    args[args.index("-c") + 1] = str(out / name)
    target = str(out / (name + ".obj"))
    if name in ("R5900.cpp", "Counters.cpp"):
        args.insert(1, f"-I{out}")
    args[args.index("-o") + 1] = target
    for flag in ("-MT", "-MF"):
        if flag in args:
            args[args.index(flag) + 1] = target + ".d"
    run(args, "compile-" + name + ".log", cwd)
    return name, args


with ThreadPoolExecutor(4) as pool:
    compiled = dict(pool.map(compile_unit, recipes.items()))
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")
archive_path = next(Path(arg) for arg in link["argv"] if Path(arg).name == "libpcsx2-hosted.a")
stock_members = {
    "R5900.cpp.obj": "3a9af7f140d91914962b0dddb71c9218c96b015d86aeb8850c9be0d74f5eb51f",
    "Counters.cpp.obj": "e68c900fd1333ad55f4e184aa11e2cf611350e9622fa61f424c6e2c67505da2a",
}
for member, expected in stock_members.items():
    actual = subprocess.check_output(["llvm-ar","p",archive_path,member])
    assert hashlib.sha256(actual).hexdigest() == expected, member
audio_object = next(Path(arg) for arg in link["argv"] if arg.endswith("/native_audio_stream.cpp.obj"))
assert hashlib.sha256(audio_object.read_bytes()).hexdigest() == (
    "f8d3ccaafd66e06bda5fe1ab3bff456346eca076b45af5d514fdf1448000ed37")
archive = out / archive_path.name
shutil.copyfile(archive_path, archive)
archive_recipe = ["llvm-ar", "rD", str(archive), *(str(out/member) for member in stock_members)]
run(archive_recipe, "archive.log")
for member in stock_members:
    assert subprocess.check_output(["llvm-ar","p",archive,member]) == (out/member).read_bytes()
(out / "archive-command.json").write_text(json.dumps(archive_recipe, indent=2) + "\n")
names = {"hosted-entry.c.obj", "ps2_vm_runner.cpp.obj", "engine.ld", "engine.elf", "engine.map", archive.name}
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
     out / "input.tile", out / "output.tile", "IZPS2 R1217"], "tile.log")
run([sys.executable, out / "build-frontend.py"], "frontend.log")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1217.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
