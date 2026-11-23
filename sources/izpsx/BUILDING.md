# Build and package IZPSX emulator

## Repackage the frozen module

Use Node.js 20.20 or newer, Python 3 with Pillow, spirv-val (SPIRV-Tools) and a compatible IZ2S Platform source checkout. The default sibling folder is `../IZ2S-Platform`; otherwise set `IZPSX_PANEL` (or `IZ2S_ROOT`) to the platform root. On Debian/Ubuntu, install the packaging tools with `sudo apt-get install python3-pil spirv-tools`; native shader rebuilding also uses `glslang-tools`.

```sh
export IZPSX_PANEL=/path/to/IZ2S-Platform
npm test
npm run package
python3 tools/release.py
```

This validates and repacks the ready `module/` tree, packages the reviewed native SD export and produces release assets in `dist/`. It does not rebuild the emulator, start a game, connect to Switch, upload data or publish a repository.

## Rebuild native code from source

On Linux, install Python 3, the packages in `requirements-build.txt`, GNU make, glslangValidator, spirv-val and an AArch64 musl GCC toolchain. Select its compiler explicitly with `IZPSX_CC` if it is not named `aarch64-linux-musl-gcc` on PATH. The sibling platform checkout supplies public native ABI metadata and IZM/SD-container builders.

```sh
export IZPSX_PANEL=/path/to/IZ2S-Platform
export IZPSX_CC=/path/to/aarch64-linux-musl-gcc
export SOURCE_DATE_EPOCH=1794293218
python3 tools/build-native.py
python3 tools/build-sd.py --hen --product-version 0.5.3
python3 tools/build-package.py
```

`build-native.py` creates a generated vendor mirror and records adapter patches without editing `vendor/`. `build-sd.py` performs independent linking/relocation checks and creates the native engine/shim. `build-package.py` creates the console shaders and module tree. The native import profile in `abi/sdk-profile.json` is required; do not replace it with arbitrary addresses from a different game/SDK.

`SOURCE_DATE_EPOCH` reproduces the build timestamp in the frozen 0.5.3 core.

After a native rebuild, export the checked module with IZ2S and update `assets/sd-release/` and its SHA-256 receipt together. Run `node tools/package.cjs --from-build` to adopt the rebuilt module, then run the tests and `tools/release.py`.

Optional reference-core tools may use `IZPSX_QEMU` or `qemu-aarch64`; emulated Linux execution is not physical Switch execution. The default test command runs local JS tests only. It does not use a BIOS/game file or automatically access the console.

## Artifacts

- `izpsx-emulator-0.5.3.iza`: platform-installable module.
- `izpsx-emulator-0.5.3.zip`: launcher.iza and matching native asset under its SD directory.
- `izpsx-emulator-0.5.3-source.zip` and `.tar.gz`: complete frozen module, corresponding source, vendor snapshot, licenses and build instructions.
- `SHA256SUMS`, `SOURCE-MANIFEST.json`, `release-report.json`: hashes and scope/limits.

The source archives omit `.git`, `build/`, `dist/`, private data, games/BIOS and local caches. No native host platform is enabled just by producing one of these archives.

The GitHub check workflow pins the compatible public [IZ2S Platform SDK](https://github.com/byizmysh/IZ2S-Platform/tree/b3de207c32375f9b5fd7191e15608f8ea3a6288b). This keeps package generation reproducible when the platform's main branch changes. Local builds can select a compatible SDK through IZPSX_PANEL.

## Native UI screenshots

Run `python3 tools/screenshots.py` with a local C compiler and Pillow. It renders the actual pause/settings code into two PNGs with synthetic controller profiles, without contacting the console. Source hashes and the capture scope are recorded in docs/images/provenance.json.

## Current native SDK variant

0.5.3 uses the interpreter and the included libsb4 headers in `sdk/include/`. Run `python3 tools/build-native.py` then `python3 tools/build-sd.py --hen --product-version 0.5.3`. The retained `--hen` switch is an internal historical build option. `abi/native-053-rebuild.json` records the native build inputs and hashes. `assets/sd-release/` contains the reviewed executable, matching engine and SHA-256 receipt. Packaging verifies and copies that frozen export; it does not silently regenerate it with whichever platform SDK happens to be installed.

After modifying native code, explicitly generate and verify a new SD export using the target platform, then update the frozen export and its receipt together. `npm run package` is a packaging check, not proof that new source has been compiled.
