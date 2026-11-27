# Compatibility - Linux / Civilization VII edition 0.34

## Target versions

| Component | Required / packaged version |
| --- | --- |
| Console | Nintendo Switch 2 |
| System firmware | 23.0.1 |
| Game | Nintendo Switch 1 cartridge of Civilization VII 1.0.0 |
| Console interface / USB keyboard | English / English (US) |
| PC panel | 0.34 |
| Console launcher | 0.34 |
| Native revision | RUN34-0.34 (local prototype) |
| Launch save policy | iz2s.sd-flex.v1 |
| PC host | Native Linux x64 / ARM64 |

**This release is Linux only.** Windows installers and Windows host executables are excluded. Windows, WSL and macOS are not supported installation targets. Other firmware/game combinations are not claimed compatible. Civilization VII is the sole entry point; historical names in source, license notices and ABI symbols do not enable an SB4 route.

Initial wireless-lobby delivery requires a compatible Wi-Fi radio, NetworkManager, the panel's network permissions and the owner's Switch 1 keys. Ethernet alone cannot perform this step. PC Link and FTP require a reachable local network. BlitBun and VacooTube use their Linux PC media dependencies.

## Integrity and compatibility

`distribution/files.json` binds the PC package. Native resources are checked against `engines/iz2s-core/runtime/package-manifest.json`; `menu-build.json` binds the recorded native inputs and outputs. The SD builder verifies runtime files, application companions and licenses, then emits `iz2s/iz2sciv-sd-manifest.json` with file sizes and SHA-256.

The RUN33 SD-flex launch save pins `bootstrap.bin`, `ro/main` and `core/main` by size and SHA-256. Install this save once to replace the earlier exact-size policy. Later compatible changes to `game/main`, `izlauncher.iza` and `izlauncher.romfs` can be installed on SD without replacing the save. The panel tracks boot compatibility separately from the full build fingerprint. A changed boot core or boot ABI still requires a matching save.

At Load Game, the three mutable files are checked by bounded headers and file extents (848 bytes in total), rather than old embedded sizes. Full SHA-256 verification remains mandatory when building and deploying a package. Native IZA section CRC validation is unchanged. Header validation cannot establish the semantic compatibility of arbitrary files or detect every possible ROMFS data corruption; use a coherent verified update set. Never change expected hashes merely to hide a mismatch. Bump `BOOT_POLICY` in `engines/IzCivSwitchFS/src/runtime-policy.cjs` when the boot ABI or save requirements change.

## Verification boundaries

Offline checks verify files, packaging and local behavior. Linux x64 installer checks do not establish Linux ARM64 execution. A successful archive build does not prove LDN delivery, console boot, application launch or startup timing. The cache fix retains shader/driver validation; initial compilation remains possible when the cache changes. See [release notes](RELEASE-NOTES.md), [source limitations](SOURCES.md) and [installation](docs/INSTALLATION.md).
