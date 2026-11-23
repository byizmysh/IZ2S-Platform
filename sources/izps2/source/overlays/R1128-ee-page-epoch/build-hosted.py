"""Build an IZPS2 worker with the archived hosted linker and R1122 components."""
import json
import shutil
import subprocess
from pathlib import Path

out = Path(__file__).resolve().parent
previous = out.parent / "R1126-hosted-engine-relink"
release = out.parent / "R1122-ee-loop-smc-epoch-charge"
objects = release / "objects"
old_link = json.loads((previous / "link-command.json").read_text())
old = Path(old_link["cwd"])
sdk = Path("${RUN23_ROOT}/engines/izhen/sdk/devkitpro")
ar = sdk / "devkitA64/bin/aarch64-none-elf-ar"
exporter = sdk.parents[3] / "tools/sd-launcher/relocatable.py"
members = out / "members"
members.mkdir(exist_ok=True)
core_archive = out / "libpcsx2-hosted.a"
shutil.copyfile(old / "pcsx2/libpcsx2.a", core_archive)

def input_object(name):
    for candidate in (out / "compiled" / name, objects / f"raw-{name}",
                      objects / name.replace(".obj", ".raw.o"), objects / name):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(name)

core_replacements = []
for name in sorted({*old_link["coreReplacements"], "Memory.cpp.obj"}):
    replacement = members / name
    shutil.copyfile(input_object(name), replacement)
    core_replacements.append(replacement)
subprocess.run([ar, "rD", core_archive, *core_replacements], check=True)
common = out / "libcommon-hosted.a"
shutil.copyfile(old / "common/libcommon.a", common)
common_members = subprocess.check_output([ar, "t", common], text=True).splitlines()
assert common_members.count("HostSys.cpp.obj") == 2
# Keep the generic HostSys (assertions, spin wait), replacing only Switch2/HostSys.
subprocess.run([ar, "dN", "2", common, "HostSys.cpp.obj"], check=True)

platform_replacements = {
    "ps2_vm_runner.cpp.obj", "native_audio_stream.cpp.obj", "video.cpp.obj",
    "native_pad.c.obj", "native_pad_frontend.cpp.obj",
}
extra = ("ps2_psx_settings_menu.c.obj", "ps2_psx_ui.c.obj", "ps2_sb4_preferences.c.obj")
image = out / "engine.elf"
args = []
for arg in old_link["argv"]:
    name = Path(arg).name
    if name in platform_replacements:
        args.append(str(input_object(name)))
    elif name == "libpcsx2-hosted.a":
        args.append(str(core_archive))
    elif name == "libcommon-hosted.a":
        args.append(str(input_object("HostSys.cpp.obj")))
        args.append(str(common))
    elif name == "izps2-hosted.elf":
        args.append(str(image))
    elif name == "izps2-hosted.map":
        args.append(str(out / "engine.map"))
    else:
        args.append(arg)
start = args.index(str(core_archive))
args[start:start] = [str(input_object(name)) for name in extra]
(out / "link-command.json").write_text(json.dumps({
    "argv": args, "cwd": str(old),
    "coreReplacements": [p.name for p in core_replacements],
    "platformReplacements": sorted(platform_replacements),
}, indent=2) + "\n")
with (out / "link.log").open("wb") as log:
    link = subprocess.run(args, cwd=old, stdout=log, stderr=subprocess.STDOUT)
if link.returncode:
    raise SystemExit(link.returncode)
subprocess.run(["python3", exporter, "--elf", image, "--output", out / "engine.izdata"], check=True)
