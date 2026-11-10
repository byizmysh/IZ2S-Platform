# Compatibility for 0.22

Release targets are Linux x64/ARM64 only. Publication does not imply qualification of every host and console combination; see the verification boundary below.

| Host | Archive/install scope | Native console scope |
| --- | --- | --- |
| Linux x64 | Source and Linux archive; install/start entry points | Declared by the included native engines, shells and bundled modules; physical coverage varies |
| Linux ARM64 | Same Linux archive; architecture-appropriate runtime selected | Declared support; no new physical ARM64 qualification claimed by packaging |
| Windows | No package or entry point | Outside the 0.22 release scope |
| macOS | No package or entry point | Outside the 0.22 release scope |

## Console paths

- SmileBASIC configured target: Switch 2 system 23.0.1, SmileBASIC 4.4.9. Fresh launch uses Make a Program → Direct after the instructed restart.
- Civ7 initial setup: Switch 1 cartridge, game version 1.0.0. Native Linux LDN uses a supported Wi-Fi radio and helper permissions; USB input uses the chosen ADV/ARM device.
- Civ7 later Launch: LAN or Wi-Fi, verified loader on TCP 9026. A ready PC does not imply the game loader is already open.
- FS acquisition is probabilistic. Retained-resource failures require restarting the game as instructed before a fresh attempt.
- RAM apps are explicitly selected. SD persistence does not imply a cold boot without the PC or unlimited offline host features.

## Bundled applications

File Exchange, File Exchange FS, Book Reader and IZPSX emulator 0.5.1 are the console IZM modules. Super File Bros. is integrated browser/file-service tooling. Other application and test packages are not included. See each module descriptor for its declared hosts and its own README for FS, controls and runtime requirements.

## Qualification boundary

This release's local verification records archive contents/checksums, source and resource contracts, regression tests and offline preview. It does not replace testing on Linux ARM64 or every supported physical console combination. Do not broaden native platform declarations without that work.

Use [host acceptance](distribution/HOST-TESTING.md), [local checks](distribution/LOCAL-CHECKS.md) and [support guidance](SUPPORT.md). Games, BIOS and keys must be supplied privately by their owners.
