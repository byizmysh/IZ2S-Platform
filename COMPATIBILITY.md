# Compatibility — Linux / Civilization VII edition 0.3.2

## Target versions

| Component | Required / packaged version |
| --- | --- |
| Console | Nintendo Switch 2 |
| System firmware | 23.0.1 |
| Game | Nintendo Switch 1 cartridge of Civilization VII 1.0.0 |
| Console interface / USB keyboard | English / English (US) |
| PC panel | 0.3.2 |
| Console launcher | 0.32 |
| Native revision | RUN32-0.32-Civ7-UI-Cache-Fix |
| PC host | Native Linux x64 / ARM64 |

**This release is Linux only.** Windows installers and Windows host executables are excluded. Windows, WSL and macOS are not supported installation targets. Other firmware/game combinations are not claimed compatible. Civilization VII is the sole entry point; historical names in source, license notices and ABI symbols do not enable an SB4 route.

Initial wireless-lobby delivery requires a compatible Wi-Fi radio, NetworkManager, the panel's network permissions and the owner's Switch 1 keys. Ethernet alone cannot perform this step. PC Link and FTP require a reachable local network. BlitBun and VacooTube use their Linux PC media dependencies.

## Integrity and compatibility

`distribution/files.json` binds the PC package. Native resources are checked against `engines/iz2s-core/runtime/package-manifest.json`; `menu-build.json` binds the recorded native inputs and outputs. The SD builder verifies runtime files, application companions and licenses, then emits `iz2s/iz2sciv-sd-manifest.json` with file sizes and SHA-256.

The game launch save embeds its selected runtime compatibility manifest. Update SD and install the matching save when those resources change. R-Entry, Menu and Civ7-UI-Cache-Fix can share the visible 0.32 number while requiring different files. Never disable checks or change expected hashes merely to hide a mismatch.

## Verification boundaries

Offline checks verify files, packaging and local behavior. Linux x64 installer checks do not establish Linux ARM64 execution. A successful archive build does not prove LDN delivery, console boot, application launch or startup timing. The cache fix retains shader/driver validation; initial compilation remains possible when the cache changes. See [release notes](RELEASE-NOTES.md), [source limitations](SOURCES.md) and [installation](docs/INSTALLATION.md).
