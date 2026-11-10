# IZ2S Platform 0.22

IZ2S Platform is a local PC control panel and console launcher system for Switch 2. Choose **SmileBASIC 4** or **Civilization VII**, connect the console, and manage applications in **IZlauncher** or **IZPSlauncher**. **IZ2S Core** is the default connection engine.

This is a **locally prepared 0.22.0 release candidate**. Nothing is uploaded or published by the build tools. Release targets are **Linux x64/ARM64 only**. Windows and macOS packages are not included. A supplied archive is not a hardware qualification claim. See [Compatibility](COMPATIBILITY.md).

[Install](#installation) · [SmileBASIC](#smilebasic-4) · [Civilization VII](#civilization-vii) · [Bundled apps](#included-applications) · [FAQ](FAQ.md) · [Release notes](RELEASE-NOTES.md) · [Support](SUPPORT.md)

## Installation

Extract the whole archive into a writable folder. Keep its files together; do not run an entry point from inside a ZIP.

| Host | Install | Start later |
| --- | --- | --- |
| Linux x64 / ARM64 | `Install-Linux.desktop`, or `sh install.sh --launch` | Application shortcut or `start.sh` |

Linux supports apt, pacman and dnf dependency installation; initial setup needs internet access. The installer prepares the PC application and does not start a console session, flash a device or install a board OS.

Use [host acceptance steps](distribution/HOST-TESTING.md) to qualify an actual machine. Native features remain subject to each component's host declaration; This release does not enable or qualify other host systems.

## SmileBASIC 4

The configured target is Switch 2 system 23.0.1 with SmileBASIC 4.4.9; other versions need verification. Choose **SmileBASIC 4**, a launcher and a connection engine. **IZ2S Core** is the default; **ns2jb by BermTheSequel** is the bundled alternative. The kurumoz original adapter is optional and fetched only on explicit request.

For the first BASIC installation, follow **First Setup** and confirm the save on the console. For each fresh launch, fully restart the console as instructed, open **SmileBASIC 4 → Make a Program → Direct**, confirm the displayed screen, and press **Load IZlauncher**. Automatic input uses Cardputer ADV or a configured ARM Linux keyboard device.

**Filesystem access** is an explicit, highlighted launch option. Acquisition is probabilistic: the panel shows an estimated **30–40% success per attempt**, not a guarantee. If an attempt reports retained or uncertain native resources, close/restart the game as instructed before another attempt. Creating a new PC attempt does not reset console memory.

## Civilization VII

Two initial delivery methods are available: **IZ2S core Wi-Fi** (LDN from a native Linux PC) and **USB** input through ADV/ARM. LDN first setup requires an appropriate Wi-Fi radio, your own compatible `prod.keys`, helper dependencies and the displayed permission command. Do not include your keys in bug reports or releases. The configured game is the Switch 1 cartridge, version 1.0.0.

**Launch supports both LAN and Wi-Fi.** After installing the loader save, open **Civilization VII → Load Game**, wait for **Awaiting payload**, select the LAN/Wi-Fi connection in the panel, and choose **Connect and start launcher**. The selected launcher starts automatically after verification. The Wi-Fi label on the initial delivery engine does not require later launches to use LDN.

Save installation is explicit and requires backup/deletion consent. It replaces the game's single-player saves with `IZ2S.Civ7Save`; never skip the backup warning. Subsequent launches load that saved loader instead of repeating chat delivery.

## Included applications

The release includes exactly these user-facing apps/tools:

| App | Use |
| --- | --- |
| [File Exchange](modules/iz-exchange/README.md) | Title-owned file access and FTP |
| [File Exchange FS](modules/iz-exchange-fs/README.md) | Retained filesystem exports; depends on File Exchange |
| [Book Reader](modules/BookReader/README.md) | TXT and unencrypted EPUB library |
| [Super File Bros.](tools/fs-rpc-browser/README.md) | Integrated browser file manager for browsing and uploads to SD |
| [IZPSX emulator 0.5.1](modules/izpsx/README.md) | PlayStation emulator; RAM/SD installation, local games/BIOS and persistent memory cards |

Super File Bros. is integrated PC tooling, not an extra console IZM payload. The four console IZM modules are built from their included descriptors and assets on startup. **Games, BIOS, prod.keys, personal books, saves, logs and device credentials are not included.** The matching IZPSX corresponding source is in [sources/izpsx](sources/izpsx).

BlitBun, VacooTube, IZDOOM, IZPS2, controller labs and experimental builds are **not bundled** in this release. Independently obtained compatible modules can still be installed through the App library.

## RAM, SD and files

**Load modules at startup** means RAM. Select apps explicitly: no previous RAM selection is restored automatically across games or sessions. App-library cards show observed **RAM** and **SD** status, installation location and installed/not-installed filters. RAM apps disappear when the game closes. SD apps persist until removed; their launch/runtime requirements are app-specific.

After connection, **Files & uploads → Open file manager** opens Super File Bros. Browse SD folders, upload files or game folders, create directories and inspect transfer progress. FTP is optional for an external client. IZPSX's Games and BIOS window uses the same file-service entry point; it does not require a separate Super File Bros. window. Exit a running native game before granting an SD file operation.

Paused uploads can resume from a verified checkpoint when the same original file is selected and the session/protocol still permit it. Do not treat an interrupted transfer as a finished file. See [FAQ](FAQ.md#uploads-and-resume).

## Offline operation and controls

Supported local SD applications may continue after losing the PC connection, once the launcher/bootstrap has started. This does not provide a complete cold boot without a PC. Streaming, host commands and PC file services need their connection. Sleep/resume and reconnect depend on the running game's retained session; use **Restore connection** when offered.

The launcher menu displays its current button hints, including **Minus: Remove**. Plus is not a global exit/reconnect binding. App controls differ; [IZPSX controls](sources/izpsx/README.md#controls) use Plus for PS1 Start and Minus for its menu. Do not apply a streaming app's shortcuts to every module.

The panel offers English, Russian, Spanish and Simplified Chinese. Console menus and individual module interfaces may have their own language scope.

## Build locally

See [BUILDING.md](BUILDING.md). `python3 distribution/release.py --rebuild` builds clean source and Linux archives and writes `SHA256SUMS` and `SOURCE-MANIFEST.json`. Build outputs are separate from personal runtime state. Build/preview checks do not execute a console game and do not establish hardware qualification.

## License and attribution

See [LICENSE](LICENSE), [LICENSE-SCOPE.md](LICENSE-SCOPE.md), [COPYRIGHT](COPYRIGHT) and [THIRD_PARTY.json](THIRD_PARTY.json). Upstream notices and component licenses remain in place. IZPSX includes PCSX-ReARMed under GPL-2.0; its corresponding source and build instructions accompany this candidate.
