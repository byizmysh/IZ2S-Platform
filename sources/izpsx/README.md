# IZPSX emulator 0.5.1

PlayStation emulation on **Switch 2**, with local ARM64 execution, an SD game library, persistent memory cards and controls for up to four players.

**IZPSX works only through [IZ2S Panel / IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform). It is not a standalone Switch application or a desktop emulator. A working console connection with Filesystem access (FS) enabled and successfully acquired is required to install and use its games, BIOS, settings and saves.** Enable FS before starting the launcher and wait for the panel to confirm access. See the platform's [installation and launch guide](https://github.com/byizmysh/IZ2S-Platform#readme) and [FS troubleshooting FAQ](https://github.com/byizmysh/IZ2S-Platform/blob/main/FAQ.md).

The emulation core is [PCSX-ReARMed](https://github.com/notaz/pcsx_rearmed), pinned in [vendor/provenance.json](vendor/provenance.json), with an Ari64 ARM64 dynarec and NEON GPU. The PC panel prepares the session, installs apps and manages files; the console runs the game.

[Install](#installation) · [Features](#what-makes-izpsx-useful) · [Screenshots](#screenshots) · [Controls](#controls) · [Files and saves](#storage-and-files) · [Build](BUILDING.md) · [FAQ](FAQ.md) · [Compatibility](COMPATIBILITY.md) · [License](LICENSE)

## Installation

1. Obtain [IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform) 0.22 with the compatible native launcher ABI. Its [README](https://github.com/byizmysh/IZ2S-Platform#readme) explains the SmileBASIC 4 and Civilization VII connection routes. This release targets Linux x64/ARM64 PC hosts.
2. Enable **Filesystem access** in the panel before launching. Wait until FS access is confirmed; an open browser tab alone does not mean that the console filesystem is available. If acquisition fails, follow the [platform FAQ](https://github.com/byizmysh/IZ2S-Platform/blob/main/FAQ.md) before another attempt.
3. In **App library**, add `izpsx-emulator-0.5.1.izm`, then choose **Session memory (RAM)** or **SD card**. The bundled platform release already includes IZPSX.
4. Open **IZPSX emulator → Games and BIOS → Connect SD card** in the panel. Provide your own 512 KiB `scph101.bin` BIOS and supported game dumps; neither is included.
5. Disconnect the file view when finished and open the IZPSX emulator tile on the console. **About** identifies whether the app was loaded from RAM or SD.

The separate SD ZIP is for an already initialized compatible IZ2S native launcher. Prefer the panel's App library so matching executable/assets, ownership and installation checks remain in force. Do not replace app or game files while the emulator is running.

## What makes IZPSX useful

### Native emulation, with two launch modes

The ARM64 dynarec, interpreter, GPU and SPU execute on the console. Games are read from SD instead of being streamed as video from the PC. **RAM** is convenient for trying an app: the panel sends its executable and matching engine for the current session. **SD** keeps the installed app, engine and data on the card. Source detection is automatic, and a RAM launch does not fall back silently to a different installed SD version.

An initialized SD entry reads its executable, games, BIOS and user state locally without a PC data service. **This still requires IZ2S Panel to establish the initial launcher/FS session. It does not provide a complete power-on boot with the PC absent.** RAM and SD use the same game, BIOS, settings and memory-card folders.

### Four player profiles and sideways Joy-Con

Each of Players 1–4 has its own input-device selection, A/B swap, stick-as-D-pad setting, deadzone and turbo Cross setting. Pro controllers, paired Joy-Con, handheld input and individual sideways Joy-Con halves are supported. Half-controller face buttons and stick axes rotate with their orientation, so menus use consistent visual button positions.

For compatible games, choose **Multitap on Port 1 or Port 2**, then select the game again. Port 1 places Players 1–4 on its four sockets. Port 2 keeps Player 1 on the first normal port and assigns Players 2–4 to the second port's Multitap. The required port and player support depend on the game: connecting four controllers does not turn a two-player title into a four-player game.

Pair individual halves with SL/SR in HOME before entering SmileBASIC. Player 1 controls emulator menus; if that player is disconnected or disabled, an available controller can restore settings.

### Display pacing that keeps the game's speed

The **Display FPS limit** offers Native, 20, 25, 30, 40, 50 and 60. It limits picture updates while emulation and sound retain the game's native timing. A 30 FPS display cap does not deliberately run a 60 Hz game at half speed; a PAL game keeps its native 50 Hz when a higher cap is selected. The performance display separates **EMU** from **VIDEO** to make this distinction visible. A cap does not fix slow emulation or guarantee a performance target.

Choose 4:3, 16:9, square pixels or integer scale; nearest or linear filtering; brightness, scanlines and overscan crop. Audio controls include volume, mute, mono, startup buffering, SPU reverb, interpolation and CD music. Emulation settings expose dynarec/interpreter selection, PS1 CPU clock, Auto/NTSC/PAL timing, BIOS logo, dithering and interlaced video. Options marked **next game launch** take effect after choosing the game again.

### Pause, change settings, or return to the launcher

**Minus** opens a native pause menu with **Continue**, **Settings**, **Restart game**, **Choose game** and **Exit IZPSX emulator**. Restart asks for confirmation. **Plus remains PS1 Start**, and R3 has no emulator exit shortcut. Pause and normal exit stop the output-audio queue so sound from the old picture is not carried into a resumed frame.

Settings are grouped into Video, Audio, Emulation, Controls, System and About tabs. Image, output-audio and control changes apply on return to play; core options for the next launch do not silently change a running game. This is an in-app pause menu, not simultaneous suspension of multiple launcher apps.

### Memory cards with recovery generations

Two shared 128 KiB PS1 memory cards are saved as alternating verified generations. Dirty cards save after the configured delay and are flushed on pause and normal exit. Writes are flushed, read back and verified before publication. If the latest generation is torn, the previous valid copy can recover it. If both copies are damaged, the emulator reports an error rather than silently formatting your progress.

Settings use their own two-generation journal. Damage to that journal does not format the memory cards. Backups are still useful: these checks do not provide an undo for deletion or protect against every SD failure. **Save states are not implemented.**

### A file manager that understands game folders

The panel can upload a complete game folder or selected CUE/BIN/IMG/PBP files, create folders and navigate the library. Before sending, it checks missing CUE tracks, unsafe paths and filename conflicts. It removes PC-specific prefixes from the uploaded CUE copy, keeps the original PC file unchanged, and sends tracks before publishing their CUE. Folder import skips unrelated covers and readme files.

The view shares the platform's [Super File Bros.](https://github.com/byizmysh/super-file-bros) file service. It does not require opening another Super File Bros. window or creating a second FS endpoint. Exit the running native game before file operations. Cancellation and same-file checkpoint continuation use the current platform service; a changed session or unverified write can require restarting the upload. BIOS/game files are never supplied by this project.

## Screenshots

The images below are freshly rendered from the **actual 0.5.1 native UI code**, using synthetic controller profiles. They show the interface, not a captured commercial game or proof of physical-console performance. Reproduce them with `python3 tools/screenshots.py`; [provenance](docs/images/provenance.json) records the source hashes.

**Pause menu — continue, settings, restart, choose another game, or return to IZlauncher.**

![IZPSX native pause menu](docs/images/pause-menu.png)

**Controls — four independent player profiles and individual sideways Joy-Con layouts.**

![IZPSX four-player controller settings](docs/images/controller-profiles.png)

## Controls

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

Keep each CUE with all of its referenced BIN/IMG tracks and matching names. PBP is supported. Choose the relevant disc before starting a multidisc title; live disc swapping is not implemented. Use your own legally obtained files. Memory-card downloads in the PC file view are read-only; back up `saves/` before maintenance. Deletion has no trash/undo.

Missing or empty game files are rejected before core startup. A missing or wrongly sized BIOS displays **BIOS NOT READY**. An unreadable image displays **GAME NOT READY**, releases ordinary file/audio/JIT resources and returns to the library without writing blank cards. Unknown transport outcomes still require a clean game restart as instructed by the panel.

## Source, compatibility and licensing

Version 0.5.1 consumes the public native SD ABI 5 service environment and tolerates compatible suffix extensions. It checks required services and SDK signatures rather than hardcoding the IZlauncher/IZPSlauncher patch version or reading private launcher-owner layouts. A future incompatible ABI still needs an update; universal future compatibility is not promised.

[src/](src/), [shaders/](shaders/), [server/](server/), the pinned [vendor snapshot](vendor/pcsx_rearmed/) and [ABI profile](abi/sdk-profile.json) accompany the frozen [module](module/). [BUILDING.md](BUILDING.md) describes portable packaging and native rebuilds. Existing SD installations must be updated once for the new public-ABI consumer; games, BIOS and saves remain user data.

The repository and its release assets are prepared locally. No remote IZPSX repository, tag or release has been created by this preparation. Packaging and local model checks do not establish new physical-console, audio, long-session or every-game qualification. See [Compatibility](COMPATIBILITY.md), [FAQ](FAQ.md) and [Changes](CHANGELOG.md).

PCSX-ReARMed and this derivative module are conveyed under GPL-2.0. Upstream notices remain intact. See [LICENSE](LICENSE), [THIRD_PARTY.md](THIRD_PARTY.md), [font license](FONT-LICENSE.txt) and the [upstream project](https://github.com/notaz/pcsx_rearmed). Games, BIOS, keys and personal saves are excluded.
