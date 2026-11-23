"""Build the NFS U2 GS queue and field measurements over the exact R1249 RUN25 recipe."""
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
base = out.parent / "R1249-ee-ordered-stall-bursts"
mtgs_base = out.parent / "R1229-gs-packet-costs"
legacy = out.parent / "R1118-ee-softmem-unaligned-pairs"
panel = out.parents[3].parents[2]
run23 = panel / "iz2s-platform-run23"
run25 = panel / "iz2s-platform-run25"
link = json.loads((base / "link-command.json").read_text())
recipes = json.loads((base / "compile-recipes.json").read_text())
mtgs_recipe = json.loads((mtgs_base / "compile-recipes.json").read_text())["MTGS.cpp"]
legacy_commands = {row["name"]: row for row in json.loads((legacy / "commands.json").read_text())}
provenance = json.loads((out / "model/source-recipe-provenance.json").read_text())
legacy_source_digests = json.loads((legacy / "build-audit.json").read_text())["sources"]

stock, = (Path(arg) for arg in link["argv"] if Path(arg).name == "libpcsx2-hosted.a")
assert stock == base / "libpcsx2-hosted.a"
assert subprocess.check_output(["llvm-ar", "p", stock, "MTGS.cpp.obj"]) == (mtgs_base / "MTGS.cpp.obj").read_bytes()


def run(args, logfile, cwd=link["cwd"]):
    with (out / logfile).open("wb") as log:
        subprocess.run([str(arg) for arg in args], cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


def compile_unit(name):
    postprocess = []
    if name in provenance:
        source = legacy_commands["compile-" + name]
        evidence = provenance[name]
        assert evidence["matchedLegacyRawAfterRestore"]
        assert sha256(subprocess.check_output(["llvm-ar", "p", stock, name + ".obj"])).hexdigest() == evidence["expected"]
        source_path = Path(source["argv"][source["argv"].index("-c") + 1])
        assert sha256(source_path.read_bytes()).hexdigest() == legacy_source_digests[str(source_path)]
        raw = out / ("raw-" + name + ".obj")
        scoped = out / ("scoped-" + name + ".obj")
        postprocess = [[*evidence["isolate"], str(raw), str(scoped)],
                       [*evidence["restore"], str(scoped), str(out / (name + ".obj"))]]
        include_source = ["-iquote", str(source_path.parent)]
    elif name == "MTGS.cpp":
        source = mtgs_recipe
        raw = out / (name + ".obj")
        include_source = []
    else:
        source = recipes[name]
        raw = out / (name + ".obj")
        include_source = []

    args = [arg.replace(str(base), str(out))
            .replace("r1249-r1175-ee-ordered-stall-bursts", "r1259-r1175-nfs-live-gs-trace")
            .replace('-DIZPS2_BUILD_REVISION="R1249"', '-DIZPS2_BUILD_REVISION="R1259"')
            for arg in source["argv"]]
    args.extend(["-I" + str(out), *include_source])
    args[args.index("-o") + 1] = str(raw)
    args[args.index("-c") + 1] = str(out / name)
    for flag in ("-MF", "-MT"):
        if flag in args:
            args[args.index(flag) + 1] = str(out / (name + ".d"))
    cwd = source.get("cwd", link["cwd"])
    run(args, "compile-" + name + ".log", cwd)
    for index, command in enumerate(postprocess):
        run(command, f"object-symbols-{name}-{index}.log", cwd)
    return name, {"argv": args, "cwd": cwd, "objectSymbolCommands": postprocess}


for name in ("hosted-entry.c", "checkpoint-extended-seed.h", "checkpoint-burst-seed.h",
             "native_ee_warm_profile.hpp", "native_ee_warm_retain.hpp", "ps2_frame_windows.hpp",
             "iop-slice-breakdown.h", "embedded-spirv.h", "mtgs-command-trace.h", "mtgs-submit-trace.h",
             "engine.ld", "ee-span-trace.hpp", "ee-affinity-native.inc", "ee-affinity-trial.hpp",
             "portable-image.py", "verify-relocation.c", "input.tile", "verify-app.cjs", "seal-image.py",
             "checkpoint-late-suffix.h", "checkpoint-late-suffix.bin", "ps2_sb4_ee_warm.inc",
             "ps2_frame_windows.inc", "ee-late-warm.hpp"):
    shutil.copyfile(base / name, out / name)
(out / "package-app.cjs").write_text((base / "package-app.cjs").read_text().replace("R1249", "R1259"))

rewritten = ("MTGS.cpp", "GSTextureCache.cpp", "GSDeviceVK.cpp")
with ThreadPoolExecutor(4) as pool:
    compiled = dict(pool.map(compile_unit, ("hosted-entry.c", "ps2_vm_runner.cpp", *rewritten)))
for name in ("R5900.cpp", "embedded-spirv.cpp", "ee-affinity-service.c", "microVU-arm64.cpp", "iR5900-arm64.cpp"):
    compiled[name] = {**recipes[name], "inheritedObject": True}
(out / "compile-recipes.json").write_text(json.dumps(compiled, indent=2) + "\n")

archive = out / stock.name
shutil.copyfile(stock, archive)
members = [out / (name + ".obj") for name in rewritten]
run(["llvm-ar", "rD", archive, *members], "archive.log")
for member in members:
    assert subprocess.check_output(["llvm-ar", "p", archive, member.name]) == member.read_bytes()
names = {"hosted-entry.c.obj", "ps2_vm_runner.cpp.obj", "engine.ld", "engine.elf", "engine.map", archive.name}
assert Counter(Path(arg).name for arg in link["argv"] if Path(arg).name in names) == Counter(names)
link["argv"] = [str(out / Path(arg).name) if Path(arg).name in names else arg for arg in link["argv"]]
(out / "link-command.json").write_text(json.dumps(link, indent=2) + "\n")
run(link["argv"], "link.log")
run([sys.executable, out / "seal-image.py", out / "engine.elf"], "seal.log")

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
     out / "input.tile", out / "output.tile", "IZPS2 R1259"], "tile.log")
(out / "frontend").mkdir(exist_ok=True)
frontend = base / "frontend/frontend.bin"
shutil.copyfile(frontend, out / "frontend/frontend.bin")
(out / "frontend-reuse.json").write_text(json.dumps({
    "inherited": str(frontend), "sha256": sha256(frontend.read_bytes()).hexdigest(),
}, indent=2) + "\n")
run(["node", out / "package-app.cjs"], "package.log")
run(["node", out / "verify-app.cjs"], "verify-app.log")
app = out / "IZPS2-R1259.iza"
(out / "SHA256SUMS").write_text(f"{sha256(app.read_bytes()).hexdigest()}  {app.name}\n")
