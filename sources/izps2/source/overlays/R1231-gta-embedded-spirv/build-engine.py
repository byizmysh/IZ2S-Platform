"""Build a single RUN25 package with a read-only table of GTA PAL shaders."""
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
base = out.parent / "R1229-gs-packet-costs"

panel = out.parents[3].parents[2]
run23 = panel / "iz2s-platform-run23"
run25 = panel / "iz2s-platform-run25"
link = json.loads((base / "link-command.json").read_text())
old_recipes = json.loads((base / "compile-recipes.json").read_text())
targets = {name: name + ".obj" for name in ("hosted-entry.c", "ps2_vm_runner.cpp", "embedded-spirv.cpp")}
shader_symbols = (
    "_ZN13VKShaderCache15GetVertexShaderESt17basic_string_viewIcSt11char_traitsIcEE",
    "_ZN13VKShaderCache17GetFragmentShaderESt17basic_string_viewIcSt11char_traitsIcEE",
    "_ZN13VKShaderCache13GetSPIRVStampEb",
)


def run(args, logfile, cwd=link["cwd"]):
    with (out / logfile).open("wb") as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


def compile_unit(name):
    recipe = old_recipes["hosted-entry.c" if name == "hosted-entry.c" else "ps2_vm_runner.cpp"]
    cwd = recipe["cwd"]
    args = [arg.replace("r1229-r1118-gs-packet-costs", "r1231-r1118-gta-embedded-spirv")
            .replace('-DIZPS2_BUILD_REVISION="R1229"', '-DIZPS2_BUILD_REVISION="R1231"')
            .replace("-I" + str(base), "-I" + str(out)) for arg in recipe["argv"]]
    if name == "ps2_vm_runner.cpp":
        args.insert(1, "-DIZPS2_GTA_EMBEDDED_SPIRV=1")
    if name == "embedded-spirv.cpp":
        args.insert(1, '-DIZPS2_SPIRV_WORDS_PATH="' + str(out / "spirv-shader-words.bin") + '"')
    args[args.index("-c") + 1] = str(out / name)
    target = str(out / targets[name])
    args[args.index("-o") + 1] = target
    for flag in ("-MT", "-MF"):
        if flag in args:
            args[args.index(flag) + 1] = target + ".d"
    run(args, "compile-" + name + ".log", cwd)
    return name, {"argv": args, "cwd": cwd}


def archive_symbols(archive, name, undefined=False):
    member = out / "model" / name
    member.write_bytes(subprocess.check_output(["llvm-ar", "p", archive, name]))
    mode = "--undefined-only" if undefined else "--defined-only"
    data = subprocess.check_output(["llvm-nm", mode, member], text=True)
    return {line.split()[-1] for line in data.splitlines() if line.strip()}


assert (out / "checkpoint-extended-seed.h").read_bytes() == (base / "checkpoint-extended-seed.h").read_bytes()
archive, = (Path(arg) for arg in link["argv"] if Path(arg).name == "libpcsx2-hosted.a")
shaderc, = (Path(arg) for arg in link["argv"] if Path(arg).name == "libshaderc_combined.a")
expected_shaderc = "5c1d17801cf0c6a57329a49399d3a919cf4e8d2aa619631ea2339d61dc866b26"
with shaderc.open("rb") as data:
    assert hashlib.file_digest(data, "sha256").hexdigest() == expected_shaderc
assert set(shader_symbols) <= archive_symbols(archive, "VKShaderCache.cpp.obj")
assert set(shader_symbols[:2]) <= archive_symbols(archive, "GSDeviceVK.cpp.obj", undefined=True)
with ThreadPoolExecutor(3) as pool:
    compiled = dict(pool.map(compile_unit, targets))
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")
names = {targets["hosted-entry.c"], targets["ps2_vm_runner.cpp"],
         "engine.ld", "engine.elf", "engine.map"}
mapping = {arg: str(out / Path(arg).name) for arg in link["argv"] if Path(arg).name in names}
assert Counter(Path(arg).name for arg in mapping) == Counter(dict.fromkeys(names, 1))
link["argv"] = [mapping.get(arg, arg) for arg in link["argv"]]
position = link["argv"].index(str(out / targets["ps2_vm_runner.cpp"])) + 1
link["argv"][position:position] = [str(out / targets["embedded-spirv.cpp"]),
    *(arg for symbol in shader_symbols[:2] for arg in ("-Xlinker", "--wrap=" + symbol))]
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
     out / "input.tile", out / "output.tile", "IZPS2 R1231"], "tile.log")
(out / "frontend").mkdir(exist_ok=True)
frontend = base / "frontend/frontend.bin"
shutil.copyfile(frontend, out / "frontend/frontend.bin")
(out / "frontend-reuse.json").write_text(json.dumps({
    "inherited": str(frontend), "sha256": hashlib.sha256(frontend.read_bytes()).hexdigest()
}, indent=2) + "\n")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1231.iza"
(out / "SHA256SUMS").write_text(f"{hashlib.sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
