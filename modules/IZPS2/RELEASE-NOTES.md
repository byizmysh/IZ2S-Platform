# IZPS2 emulator 0.5.0

IZPS2 runs PlayStation 2 games on Nintendo Switch 2 through IZ2S.

## Install

Requires IZ2S RUN25 with native ABI 7 and filesystem access.

1. Import `izps2-emulator-0.5.0.iza` through **App library** in IZ2S and install it to the Switch 2 SD card. The installer contains the launcher, ARM64 engine, shaders, fonts, and interface assets.
2. Copy your 4 MiB PS2 BIOS dump to `SD:/iz2s/modules/izps2/bios/` and your ISO or CHD images to `SD:/iz2s/modules/izps2/games/`.
3. Open **IZPS2 emulator** in IZ2S.

## Civ7 Stability-1 local overlay

This package includes the console-tested Owner-Writer-Trial-1 engine and its exit diagnostics. The Civ7 panel includes `vu-split-persist-off.flag` in both the full SD kit and individual IZPS2 export archives. Extract the SD kit to install the flag. The native Add app / PC Link importer preserves an existing flag but does not extract this auxiliary file from an app archive. Optional VU disk persistence is disabled; ordinary JIT execution and game saves remain enabled. This is a stability mitigation, not an upstream IZPS2 0.5.0 binary or a confirmed repair of the cache writer. See the platform release notes for validation limits.
