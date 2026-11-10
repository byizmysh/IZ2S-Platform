# IZPSX emulator 0.5.0

A PlayStation emulator module for **IZ2S Platform** and its native launcher ABI. Emulation runs on Switch 2; the PC panel installs the module, starts RAM sessions and provides an optional game/BIOS file manager. The underlying CPU/GPU/SPU implementation is the pinned [PCSX-ReARMed](vendor/provenance.json) source with an Ari64 ARM64 dynarec and NEON GPU.

This repository and its GitHub release assets are **prepared locally**. No remote repository, tag or release has been created by these tools. The included 0.5.0 console payload is the exact frozen module selected for the IZ2S Platform 0.22 candidate. Local build/model checks are not a claim of new physical-console qualification or universal game compatibility.

[Installation](#installation) · [Controls](#controls) · [Storage](#storage-and-files) · [Build](BUILDING.md) · [FAQ](FAQ.md) · [Changes](CHANGELOG.md) · [License](LICENSE)

## Installation

1. Start a compatible IZ2S Platform installation and obtain its console filesystem access.
2. Add `izpsx-emulator-0.5.0.izm` through the App library. Select **Session memory (RAM)** for a temporary loaded copy or **SD card** for persistent installation.
3. Supply your own 512 KiB `scph101.bin` BIOS and supported game dumps. They are not included.
4. Open the IZPSX emulator tile on the console. The app detects its RAM/SD source automatically; its About view displays that source.

The module currently declares **Linux x64 and Linux ARM64 PC hosts**. The console's ARM64 payload is separate from PC host support. The 0.22 platform bundle targets Linux only; Windows and macOS builds are outside this release scope. See [Compatibility](COMPATIBILITY.md).

The standalone SD ZIP is for an already initialized compatible native launcher. Use the platform's App library when possible so its ownership, matching assets and installation checks stay in force. Do not replace files while a game is running.

## Controls

- **Plus (+)**: PS1 Start in a game.
- **Minus (-)**: IZPSX menu/library settings. Choose **Exit IZPSX emulator** to return to the launcher.
- **L/R**: change settings tabs.
- **ZL/ZR / player selector**: choose the player profile, up to four players where the game's Multitap support allows it.
- **B**: save profiles; **X** resets one setting; **Y** resets the selected player's settings.
- **R3** has no global exit shortcut in this version.

Each player has separate device assignment, button swaps, stick/D-pad choice, deadzone and turbo settings. Choose Off to disconnect game input. The system-disconnection action is separate. For a single sideways Joy-Con, pair it using SL/SR before entering SmileBASIC; follow the controller hints for shoulder buttons and the menu chord. Multitap support is game-specific; select the game again after changing its port setting.

## Storage and files

```text
SD:/IZ2S/Homebrew/Modules/izpsx/
  launcher.izapp
  asset-<sha256>.izdata
  bios/scph101.bin
  games/<game folder>/<disc>.cue
  games/<game folder>/<track>.bin
  saves/card1.a.izmc
  saves/card1.b.izmc
  saves/card2.a.izmc
  saves/card2.b.izmc
  settings.a.izcfg
  settings.b.izcfg
```

Keep CUE and its BIN/IMG tracks together with matching names. PBP is supported. Choose the relevant disc entry before a multidisc game; live disc swapping and save states are not implemented. Memory-card saves use verified generations; back up the saves directory before maintenance.

For uploads, open **IZPSX emulator → Games and BIOS → Connect SD card** in the panel. Exit the running native game first through Minus → Exit IZPSX emulator. This view shares the platform's Super File Bros. endpoint; it does not need a second file-manager window or a separate FS engine. Upload complete folders, validate missing tracks, and select the same source file when continuing a resumable transfer. Partial or unknown writes must not be treated as completed games.

RAM and SD use the same game/BIOS/settings folders. A RAM launch sends the matching executable/engine; it does not silently fall back to an unrelated SD version. An initialized SD entry reads local game data without a PC data service, but the launcher bootstrap still needs the PC for initial startup. A complete cold boot with no PC is not provided.

## Source, packages and provenance

- `src/`, `shaders/`, `server/`: frontend, native ABI integration, GPU resources and local panel UI.
- `vendor/pcsx_rearmed/`: pinned corresponding core source and notices; no submodule checkout is needed.
- `abi/sdk-profile.json`: native import profile used by this release.
- `module/`: checked, ready-to-package console and server assets matching the selected version.
- `tools/`: local build and packaging tools; no tool automatically installs on a console or publishes online.

Native builds require the compatible IZ2S SDK checkout and an AArch64 musl toolchain. [BUILDING.md](BUILDING.md) documents the environment and exact steps. `tools/release.py` creates the IZM, an SD ZIP, source ZIP/tarball and checksums; it never creates a remote GitHub project.

PCSX-ReARMed and this derivative module are conveyed under GPL-2.0; upstream notices remain in the vendor tree. See [LICENSE](LICENSE), [THIRD_PARTY.md](THIRD_PARTY.md) and the font license. Games, BIOS, keys, commercial assets, personal saves, performance captures and crash dumps are excluded.
