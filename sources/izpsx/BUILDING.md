# Build and package IZPSX emulator

## Repackage the frozen module

Use Node.js 20.20 or newer and a compatible IZ2S Platform source checkout. The default sibling folder is `../IZ2S-Platform`; otherwise set `IZPSX_PANEL` (or `IZ2S_ROOT`) to the platform root.

```sh
export IZPSX_PANEL=/path/to/IZ2S-Platform
npm test
npm run package
python3 tools/release.py
```

This validates and repacks the ready `module/` tree, builds its native SD container with the public platform ABI and produces release assets in `dist/`. It does not rebuild the emulator, start a game, connect to Switch, upload data or publish a repository.

## Rebuild native code from source

On Linux, install Python 3, the packages in `requirements-build.txt`, GNU make, glslangValidator, spirv-val and an AArch64 musl GCC toolchain. Select its compiler explicitly with `IZPSX_CC` if it is not named `aarch64-linux-musl-gcc` on PATH. The sibling platform checkout supplies public native ABI metadata and IZM/SD-container builders.

```sh
export IZPSX_PANEL=/path/to/IZ2S-Platform
export IZPSX_CC=/path/to/aarch64-linux-musl-gcc
python3 tools/build-native.py
python3 tools/build-sd.py
python3 tools/build-package.py
```

`build-native.py` creates a generated vendor mirror and records adapter patches without editing `vendor/`. `build-sd.py` performs independent linking/relocation checks and creates the native engine/shim. `build-package.py` creates console shaders and the IZM/SD bundle. The native import profile in `abi/sdk-profile.json` is required; do not replace it with arbitrary addresses from a different game/SDK.

After a deliberate native rebuild, run `node tools/package.cjs --from-build` to replace the frozen module tree with that checked build, then re-run tests and `tools/release.py`. Compare hashes and record hardware qualification before publishing changed native code. Repacking the existing `module/` does not claim a native rebuild.

Optional reference-core tools may use `IZPSX_QEMU` or `qemu-aarch64`; emulated Linux execution is not physical Switch execution. The default test command runs local JS tests only. It does not use a BIOS/game file or automatically access the console.

## Artifacts

- `izpsx-emulator-0.5.0.izm`: platform-installable module.
- `izpsx-emulator-0.5.0-sd.zip`: launcher.izapp and matching native asset under its SD directory.
- `izpsx-emulator-0.5.0-source.zip` and `.tar.gz`: complete frozen module, corresponding source, vendor snapshot, licenses and build instructions.
- `SHA256SUMS`, `SOURCE-MANIFEST.json`, `release-report.json`: hashes and scope/limits.

The source archives omit `.git`, `build/`, `dist/`, private data, games/BIOS and local caches. No native host platform is enabled just by producing one of these archives.

The GitHub check workflow uses the platform's main branch as its SDK. Publish the matching IZ2S Platform 0.22 source first; an older public main may lack the required SD/native ABI. No remote workflow has been executed as part of this local preparation.
