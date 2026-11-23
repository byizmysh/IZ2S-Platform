# IZPSX emulator

<p align="center"><img src="assets/brand/izpsx-icon-v1.png" width="120" alt="IZPSX emulator icon"></p>

IZPSX runs PlayStation games on Nintendo Switch 2 through [IZ2S](https://github.com/byizmysh/IZ2S-Platform). It includes an SD game library, persistent memory cards, and controls for up to four players.

[Install](#install) · [BIOS and games](#bios-and-games) · [Features](#what-makes-izpsx-useful) · [Screenshots](#screenshots) · [Controls](#controls) · [Files and saves](#storage-and-files) · [Build](BUILDING.md) · [FAQ](FAQ.md) · [Compatibility](COMPATIBILITY.md) · [License](LICENSE)

## Install

### Through IZ2S App library

1. Download [`izpsx-emulator-0.5.3.iza`](https://github.com/byizmysh/IZPSX-emulator/releases/download/v0.5.3/izpsx-emulator-0.5.3.iza).
2. In IZ2S, open **App library**, import the `.iza` file, and install it to the Switch 2 SD card. IZ2S installs the emulator in `SD:/iz2s/modules/izpsx/`. The installer includes the launcher, engine, shaders, font, and interface assets.

### Directly to the SD card

1. Download [`izpsx-emulator-0.5.3.zip`](https://github.com/byizmysh/IZPSX-emulator/releases/download/v0.5.3/izpsx-emulator-0.5.3.zip).
2. Extract the ZIP to the root of the Switch 2 SD card so `iz2s/` sits at the top level. The archive includes the emulator files and empty `bios/`, `games/`, and `saves/` folders under `SD:/iz2s/modules/izpsx/`.

Preserve your BIOS, games, memory cards, and settings when updating.

## BIOS and games

| File | SD card path |
| --- | --- |
| Your own PlayStation BIOS, exactly **512 KiB (524,288 bytes)** | `SD:/iz2s/modules/izpsx/bios/bios.bin` |
| CUE/BIN or CUE/IMG disc image | `SD:/iz2s/modules/izpsx/games/<game name>/`. Keep the CUE file and its referenced tracks together. |
| PBP game image | `SD:/iz2s/modules/izpsx/games/<game name>/` |

Open **IZPSX emulator** in IZ2S and select a game.

You can also use **Games and BIOS** in IZ2S when its PC integration is available. Enable filesystem access and connect the SD card before transferring files. [IZ2S setup](https://github.com/byizmysh/IZ2S-Platform#readme) · [Filesystem help](https://github.com/byizmysh/IZ2S-Platform/blob/main/FAQ.md)

## What makes IZPSX useful

- **Native emulation, RAM or SD.** The ARM64 core runs on the console. RAM loads the app for the current session; SD keeps it installed with its engine and data. Both use the same game, BIOS, settings and save folders.

- **Up to four players.** Each player has a device choice, A/B swap, stick-as-D-pad, deadzone and turbo Cross. Pro controllers, paired Joy-Con, handheld controls and sideways Joy-Con halves are supported. Choose Multitap on Port 1 or 2 for compatible games, then relaunch the game. Pair individual halves with SL/SR in HOME.

- **Picture and performance.** Display limits: Native, 20, 25, 30, 40, 50 or 60 FPS, with native emulation/audio timing preserved and separate EMU/VIDEO counters. Adjust aspect ratio, integer scaling, filtering, brightness, scanlines and overscan. A display cap does not fix slow emulation.

- **Audio and core settings.** Volume, mute, mono, buffering, SPU reverb/interpolation and CD music; interpreter, PS1 CPU clock and Auto/NTSC/PAL timing. Options marked **next game launch** apply after selecting the game again.

- **Pause and return.** **Minus** opens Continue, Settings, Restart game, Choose game and Exit. **Plus = PS1 Start**; R3 has no exit shortcut. Video, audio and control changes apply on return to play. Pause and normal exit stop queued audio.

- **Persistent memory cards.** Two shared 128 KiB cards save to alternating verified generations, with flushing on pause/exit and recovery from a damaged latest copy. Settings use a separate journal. Keep backups; **save states are not implemented**.

- **Game-folder management.** Upload folders or CUE/BIN/IMG/PBP files, create directories and browse the library. The panel checks CUE tracks, paths and filename conflicts, adjusts the uploaded CUE copy and sends tracks first. It shares the platform's [Super File Bros. service](https://github.com/byizmysh/IZ2S-Platform/blob/main/tools/fs-rpc-browser/README.md). Transfers support cancellation and verified-checkpoint continuation where the session permits. Exit the running game before file operations.

## Screenshots

**Pause menu — continue, settings, restart, choose another game, or return to IZ2S.**

![IZPSX native pause menu](docs/images/pause-menu.png)

**Controls — four independent player profiles.**

![IZPSX four-player controller settings](docs/images/controller-profiles.png)

## Controls

In Controls, leave Player 1 → Device on **AUTO** to switch between attached handheld Joy-Con and the first external controller automatically. Attached controls take priority while available, even if a wireless Pro Controller stays connected. Explicit device choices remain fixed.

| Input | Action |
| --- | --- |
| A / B / D-pad | Select / back / navigate |
| Y in the library | Open the last played game |
| Plus (+) in a game | PS1 Start |
| Minus (-) | Emulator pause menu, or library settings |
| L3 / stick click | PS1 Select |
| L/R in settings | Change tabs |
| ZL/ZR / player selector | Choose Player 1–4 |
| Left/Right in settings | Change a value |
| X / Y in settings | Restore one value / reset current tab with confirmation |
| B in settings | Save and return |

For an individual sideways Joy-Con, +/- is Start and stick click is Select. SL/SR are L1/R1; outer shoulder/trigger buttons are L2/R2. **Hold SL + SR and press the half's +/-** to open the IZPSX menu. In settings, SL/SR changes tabs and outer shoulders select a player. Choose **Exit IZPSX emulator** in the pause menu to return to the launcher.

## Storage and files

```text
SD:/iz2s/modules/izpsx/
  launcher.iza
  asset-<sha256>.izdata
  bios/bios.bin
  games/<game folder>/<disc>.cue
  games/<game folder>/<track>.bin
  saves/card1.a.izmc
  saves/card1.b.izmc
  saves/card2.a.izmc
  saves/card2.b.izmc
  settings.a.izcfg
  settings.b.izcfg
```

Keep each CUE with all of its referenced BIN/IMG tracks and matching names. PBP is supported. Choose the relevant disc before starting a multidisc title; live disc swapping is not implemented. Use your own legally obtained files. Memory-card downloads in the PC file view are read-only; back up `saves/` before maintenance. Deletion has no trash/undo.

Missing or empty game files are rejected before core startup. A missing or wrongly sized BIOS displays **BIOS NOT READY**. An unreadable image displays **GAME NOT READY**, releases ordinary file/audio/JIT resources and returns to the library without writing blank cards. Unknown transport outcomes still require a clean game restart as instructed by the panel.

## Source, compatibility and licensing

[src/](src/), [shaders/](shaders/), [server/](server/), the pinned [vendor snapshot](vendor/pcsx_rearmed/) and [ABI profile](abi/sdk-profile.json) accompany the frozen [module](module/). [BUILDING.md](BUILDING.md) describes portable packaging and native rebuilds.

The [repository](https://github.com/byizmysh/IZPSX-emulator) and [release downloads](https://github.com/byizmysh/IZPSX-emulator/releases) include the matching native module and corresponding source. See [Compatibility](COMPATIBILITY.md), [FAQ](FAQ.md) and [Changes](CHANGELOG.md).

PCSX-ReARMed and this derivative module are conveyed under GPL-2.0. Upstream notices remain intact. See [LICENSE](LICENSE), [THIRD_PARTY.md](THIRD_PARTY.md), [font license](FONT-LICENSE.txt) and the [upstream project](https://github.com/notaz/pcsx_rearmed). Games, BIOS, keys and personal saves are excluded.
