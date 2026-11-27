#!/usr/bin/env python3
"""Prepare a separate checked RUN33 panel and SD export for a native split build."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


def read(path):
    return json.loads(path.read_text())


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")


def identity(path):
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"bytes": path.stat().st_size, "sha256": digest}


def package(base, native, output, version):
    revision = f"RUN33-Split-v{version}-test"
    build_id = revision.lower()
    candidate = output / f"RUN33-Split-v{version}"
    candidate.mkdir(parents=True, exist_ok=False)
    inventory = read(base / "distribution/files.json")
    built = read(native / "build-inputs.json")
    engine_path = "iz2s/modules/split-runtime.izdata"
    engine_name = "engines/iz2s-core/runtime/package/" + engine_path
    inventory["files"].pop(engine_name, None)
    names = set(inventory["files"])
    overlay = base / "sources/run32-launcher"
    names.update("sources/run32-launcher/" + name for name in built["sources"])
    for path in overlay.rglob("*"):
        if path.is_file() and (path.name.startswith("split-") or
                               path.name in {"build-split.py", "package-split.py"}):
            names.add(path.relative_to(base).as_posix())
    for name in sorted(names):
        source, dest = base / name, candidate / name
        overlay_path = source.relative_to(overlay).as_posix() if source.is_relative_to(overlay) else None
        if overlay_path in built["sources"]:
            source = native / overlay_path
            assert built["sources"][overlay_path] == identity(source)["sha256"], name
        elif name in inventory["files"]:
            recorded = inventory["files"][name]
            assert identity(source) == {k: recorded[k] for k in ("bytes", "sha256")}, name
        assert source.is_file() and not source.is_symlink(), name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
    (candidate / "data").mkdir()
    (candidate / "dist").mkdir()

    runtime = candidate / "engines/iz2s-core/runtime"
    main = runtime / "package/game/main"
    exefs = runtime / "package/game/exefs.nsp"
    previous = identity(main)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(base / "engines/iz2s-core/tools"))
    from game_package import build, parse

    stock = parse(exefs.read_bytes())
    assert stock["main"] == main.read_bytes()
    binary = (native / "main").read_bytes()
    assert binary[:4] == b"NSO0"
    assert hashlib.sha256(binary).hexdigest() == built["mainSha256"]
    main.write_bytes(binary)
    exefs.write_bytes(build(binary, stock["main.npdm"]))
    assert parse(exefs.read_bytes()) == {"main": binary, "main.npdm": stock["main.npdm"]}

    manifest_path = runtime / "package-manifest.json"
    manifest = read(manifest_path)
    assert "splitEngine" not in built, "NRO Split runtime is no longer supported"
    manifest = [row for row in manifest if row["path"] != engine_path]
    for record in manifest:
        path = runtime / "package" / record["path"]
        current = identity(path)
        if path in (main, exefs):
            record.update(current)
        else:
            assert current == {k: record[k] for k in ("bytes", "sha256")}, record["path"]
    write(manifest_path, manifest)

    menu_path = runtime / "menu-build.json"
    menu = read(menu_path)
    menu["componentRevision"] = revision
    font = "language/fonts/DejaVuSans-Bold.ttf"
    assert identity(candidate / font)["sha256"] == built["fontSha256"]
    menu["inputs"][font] = built["fontSha256"]
    for name in names:
        if name.startswith("sources/run32-launcher/"):
            menu["inputs"][name] = identity(candidate / name)["sha256"]
    for path in (main, exefs):
        menu["outputs"][path.relative_to(candidate).as_posix()] = identity(path)["sha256"]
    menu["outputs"].pop(engine_name, None)
    write(menu_path, menu)

    provenance_path = runtime / "provenance.json"
    provenance = read(provenance_path)
    previous_revision = provenance["launcher"]["revision"]
    provenance["launcher"].update(revision=revision, coreSha256=identity(main)["sha256"],
                                  consoleLaunchVerified=False,
                                  crashFixConfirmedOnConsole=False,
                                  splitUiFont="DejaVu Sans Bold")
    provenance["launcher"].pop("splitRuntime", None)
    provenance["launcher"].update(splitFormats=["IZA"], nroLaunchMode="full-screen")
    provenance["localChanges"].append(
        "NRO Split and its offscreen compatibility runtime have been removed. "
        "IZA Split and full-screen NRO handoff are preserved. "
        "Load Game crash cause is unconfirmed; Switch validation is pending.")
    write(provenance_path, provenance)

    validation_path = runtime / "run32-component-validation.json"
    write(validation_path, {
        "format": read(validation_path)["format"],
        "revision": revision, "consoleLaunchVerified": False,
        "crashFixConfirmedOnConsole": False, "nativeReproducedTwice": False,
        "npdmUnchanged": True, "standaloneNroDistributed": False, "menuId": menu["menuId"],
        "components": {name: identity(runtime / "package" / name) for name in
                       ("game/main", "game/exefs.nsp", "iz2s/izlauncher.iza", "iz2s/izlauncher.romfs")},
    })

    runtime_manifest_path = runtime / "manifest.json"
    runtime_manifest = read(runtime_manifest_path)
    runtime_manifest["files"] = [row for row in runtime_manifest["files"]
                                 if row["path"] != "package/" + engine_path]
    for record in runtime_manifest["files"]:
        record.update(identity(runtime / record["path"]))
    write(runtime_manifest_path, runtime_manifest)

    profile_path = candidate / "distribution/profile.json"
    profile = read(profile_path)
    profile.update(buildId=build_id, publicationStatus="local-candidate")
    write(profile_path, profile)
    for name in ("COMPATIBILITY.md", "engines/IzCivSwitchFS/README.md"):
        document = candidate / name
        document.write_text(document.read_text().replace(previous_revision, revision))
    (candidate / "README-RUN33.md").write_text(
        f"# {revision}\n\n"
        "Use the original IZtwiNS2 and IZPSX application files. Both can occupy either pane. "
        "Compatible Vulkan-only apps with private control buffers are also selectable.\n\n"
        "- Press R + Up in the launcher. The Split row under the cards shows the next step. "
        "Choose each app card with A. The pair opens after the second selection. "
        "Press B or R + Up again to cancel before the pair opens.\n"
        "- Press Plus and Minus together to open the Full HD window controls. "
        "Repeat the chord or press B to hide the controls and window frames.\n"
        "- Use Left/Right and A, or tap a control. X switches focus and Y cycles audio while the controls are open.\n"
        "- Tap a pane to focus it. The light blue frame marks the active pane. The first tap on an inactive pane selects it.\n"
        "- Swap changes sides in Split and exchanges foreground and background in Float.\n"
        "- In Float, drag the floating title bar to an edge or choose Corner. Smaller/Larger selects one of five sizes, about 1/8 to 1/4 of screen area.\n\n"
        "With controls open, the blue frame shows focus and the speaker icon shows audio. Tap a speaker to toggle that app's sound.\n\n" +
        "NRO apps use a full-screen handoff and cannot join this split session. "
        "The NRO Split compatibility runtime has been removed.\n\n" +
        "When an app exits normally, the other remains open. The launch save "
        "must use the matching boot core. Install the SD-flex save once to allow future compatible SD updates. "
        "Console execution is unverified.\n")

    for name in sorted(names):
        path = candidate / name
        inventory["files"][name] = {**identity(path), "executable": bool(path.stat().st_mode & 0o111)}
    write(candidate / "distribution/files.json", inventory)
    subprocess.run(["node", "distribution/verify.cjs"], cwd=candidate, check=True)
    subprocess.run(["node", "engines/IzCivSwitchFS/tools/sd-package.mjs"],
                   cwd=candidate, check=True)
    exported = candidate / "engines/IzCivSwitchFS/dist/iz2sciv-core-sd.zip"
    archive = output / f"IZ2S-RUN33-Split-v{version}-SD-test.zip"
    shutil.copy2(exported, archive)

    with zipfile.ZipFile(archive) as zipped:
        assert not zipped.testzip()
        assert engine_path not in zipped.namelist()
        assert zipped.read("iz2s/core/bin/game/main") == binary
        assert all(name.startswith("iz2s/") and not name.endswith("IZ2S.nro")
                   for name in zipped.namelist())
        metadata = json.loads(zipped.read("iz2s/iz2sciv-sd-manifest.json"))
        assert metadata["buildId"] == build_id and metadata["revision"] == revision
        for row in metadata["files"]:
            contents = zipped.read(row["path"])
            assert len(contents) == row["bytes"]
            assert hashlib.sha256(contents).hexdigest() == row["sha256"]
    write(output / f"package-v{version}.json", {
        "revision": revision, "panel": str(candidate), "main": identity(main),
        "stockMain": previous, "archive": str(archive), **identity(archive),
        "consoleVerified": False,
    })


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("native", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("version", type=int)
    args = parser.parse_args()
    package(args.base.resolve(), args.native.resolve(), args.output.resolve(), args.version)
