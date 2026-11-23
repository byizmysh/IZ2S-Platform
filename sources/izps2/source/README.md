# IZPS2 emulator

IZPS2 runs PlayStation 2 games on Nintendo Switch 2 through IZ2S. It uses the ARM64 core from ARMSX2 and PCSX2 and supports virtual memory cards, controller settings, and per-game save states.

## Compatibility

| [IZ2S](https://github.com/byizmysh/IZ2S-Platform) | KaiHEN |
| --- | --- |
| Works | Скоро |

Version **0.5.0** requires IZ2S RUN25 with native ABI 7 and filesystem access.

## BIOS and games

Copy the files from your own PlayStation 2 and discs to these folders on the Switch 2 SD card. Create the folders if needed.

| File | SD card folder | Format |
| --- | --- | --- |
| **PS2 BIOS** | `SD:/iz2s/modules/izps2/bios/` | Raw **4 MiB** BIOS dump, for example `bios.bin` |
| **PS2 games** | `SD:/iz2s/modules/izps2/games/` | **ISO** (`.iso`) or **CHD** (`.chd`) disc images |

## Install

1. Download [`izps2-emulator-0.5.0.iza`](https://github.com/byizmysh/IZPS2-emulator/releases/download/v0.5.0/izps2-emulator-0.5.0.iza).
2. Open **App library** in IZ2S, import the `.iza` file, then install it to the Switch 2 SD card. The file includes the launcher, engine, shaders, fonts, and interface assets.
3. Put your BIOS and games in the folders above. Open **IZPS2 emulator** in IZ2S and select a game.

Keep your BIOS, games, saves, and settings when updating. Press **Minus** during gameplay for the emulator menu.

## Source and licenses

The [source tree](source/) includes the code, build recipes, and license notices for developers who want to inspect the code, fix bugs, port it, or build their own version.

See [Building](BUILDING.md), [Changelog](CHANGELOG.md), and [Third-party notices](THIRD_PARTY.md).
