# IZ2S Platform 0.25

IZ2S is a Homebrew launcher for Nintendo Switch.

## Download 0.25

| System | Download |
| --- | --- |
| Linux x64 / ARM64 | [Linux installation archive](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.0/IZ2S-Platform-0.25-linux.tar.gz) |
| Windows x64 | [Windows installation ZIP](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.0/IZ2S-Platform-0.25-windows-x64.zip) |
| File checksums | [SHA256SUMS.txt](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.0/SHA256SUMS.txt) |

Use the installation archives above. GitHub's automatic **Source code** downloads are for development, not the packaged installers.

## Before installation

Extract the entire archive into a new folder. Keep the folder structure intact. Installation creates a separate per-user copy; do not extract over an existing installation. Network access is required when the installer needs to download host dependencies.

## Linux x64 / ARM64 installation

Open a terminal in the extracted folder and run this single command:

```sh
sh ./install.sh --launch
```

Setup installs the missing prerequisites, verifies the installation, creates desktop and application-menu shortcuts and opens the panel automatically. Your package manager may request your password. No manual Node.js or Python setup is required on supported Linux systems.

The installer checks Python 3, Pillow, curl, tar, xz and SSH; it can install system prerequisites with your package manager and prepare Node.js 24 if needed. The installer prints the final destination.

For later launches, choose **IZ2S Platform 0.25** from the application menu, or open the installed folder and run:

```sh
./start.sh
```

The default location is a new versioned folder under:

```text
~/.local/share/iz2s-platform/releases/
```

The installer does not require an existing Node.js installation if it can download the private runtime. To choose a destination without system package changes or desktop shortcuts:

```sh
./install.sh --prefix /absolute/path/iz2s-platform --no-system-packages --no-desktop
```

## Windows x64 installation

Extract the complete ZIP with **Extract All**, then double-click:

```text
Install.cmd
```

Wait until setup prints **Installed** and opens the panel automatically. For later launches, use the **IZ2S Platform** desktop or Start-menu shortcut, or open the printed destination folder and double-click:

```text
Start.vbs
```

The desktop and Start-menu shortcuts include the IZ2S icon and open the panel without a console window. For an existing installation, use those shortcuts or Start.vbs in the installed folder. Start.cmd is retained as a console diagnostic entry point; do not start files from the downloaded ZIP. Setup prepares private Node.js, Python and Pillow, and an SSH client if missing. Windows x64 is required; Windows ARM64 and 32-bit systems are not supported. The panel and console packages are included; the Linux PC media hosts and Civ7 Wi-Fi setup are not Windows-supported features.

Windows may show its standard firewall permission dialog for Node.js. Allow access on your private network when connecting a console; the local panel itself works without opening incoming network access.

## Opening the panel and checking the installation

The panel normally opens at:

```text
http://127.0.0.1:39524
```

PC Link uses port **39525**. Installation verifies the packaged files automatically. The following optional commands use the private runtime from the installed folder; global Node.js/npm is not required.

Preview on Linux (does not establish a console session):

```sh
./preview.sh
```

Preview on Windows:

```powershell
.\data\runtime\node\node.exe .\start.cjs --preview --no-open
```

Use the URL printed by the Preview process. To repeat the release-content checks on Linux:

```sh
./tools/node distribution/verify.cjs
```

On Windows:

```powershell
.\data\runtime\node\node.exe .\distribution\verify.cjs
```

## Included applications

| Application | Version | Purpose | Restrictions |
| --- | --- | --- | --- |
| [BlitBun](https://github.com/byizmysh/BlitBun) | 1.15.8 | PC video/audio streaming | Only Linux |
| Book Reader | 0.1.0 | TXT/EPUB reader | |
| Foldrift | 0.3.3 | Autonomous SD file manager | |
| [IZ2S Cheese](https://github.com/byizmysh/iz2s-cheese) | 0.8.0 | Homebrew application catalog | |
| [VacooTube](https://github.com/byizmysh/VacooTube) | 1.5.7 | Video player with PC source | Only Linux |
| [IZPSX emulator](https://github.com/byizmysh/IZPSX-emulator) | 0.5.2 | PlayStation emulator | |

Open **App library** in the panel and install the selected application to the paired console. For a mounted SD card, use the platform's SD installation/export action. The installed entry is:

```text
SD:/iz2s/modules/<id>/launcher.iza
```

Keep every exported companion resource, manifest and signature beside the executable.

For Foldrift:

```text
SD:/iz2s/modules/foldrift/launcher.iza
```

For IZ2S Cheese:

```text
SD:/iz2s/modules/iz2s-cheese/launcher.iza
```

A PC download may be a package containing resources; do not simply rename that package to `launcher.iza`.

BlitBun uses its panel tab to select a browser share or a PC window. Native window/audio capture uses Linux, FFmpeg, X11/XWayland and PulseAudio/PipeWire (`pactl`/`parec`). Browser sharing requires an explicit selection in the browser. VacooTube includes its PC application source, streaming server and, in the Linux archive, clean Electron runtimes for x64/ARM64. No browser profile or login is included. These Linux media helpers are not supported on Windows; application files remain available for installation to Switch. Book Reader imports TXT/EPUB through its panel settings. IZ2S Cheese uses a configured catalog and its own download interface.

## IZPSX BIOS and games

BIOS and game images are not included. Add your own **512 KiB (524,288-byte)** BIOS at:

```text
SD:/iz2s/modules/izpsx/bios/bios.bin
```

Put games in:

```text
SD:/iz2s/modules/izpsx/games/<game name>/
```

Keep each CUE file with all referenced BIN/IMG tracks; put PBP files in the same per-game layout.

When updating, preserve settings and these folders:

```text
bios/
games/
saves/
```

The updated 0.5.2 native image and PC file interface both use these paths.

## Connections and local state

Select the input device and network adapters in the panel. Automatic discovery depends on a reachable local network; enter the device's displayed address when discovery is unavailable. Use the port actually shown in the console's FTP indicator; older installed runtimes can differ from the current **default of 1425**. Reopening the console menu is required to activate a newly installed native runtime.

Personal settings, pairing keys and logs are created on first use under:

```text
data/
```

Or in the owning module's state directory. No credentials, Nintendo keys, BIOS, games, save files or previous sessions are supplied. Civ7 currently has no filesystem access; its Wi-Fi setup requires Linux networking tools. No delivery date is promised by this release.

## Authors, upstream work and licenses

**IZMYSH (@byizmysh)** authors and maintains IZ2S integration and the application adaptations. This project relies on work by many other authors; renaming an interface does not remove their authorship or license terms.

- **BermTheSequel, pepsipu and kurumuz**: the ns2jb family and inherited console connection work. Exact retained origins/revisions are in the engine provenance and installer notices: [ns2jb2](https://gitlab.com/BermTheSequel/ns2jb2), [pepsipu/ns2jb](https://gitlab.com/pepsipu/ns2jb/), [kurumuz/ns2jb](https://gitlab.com/kurumuz/ns2jb).
- **ouncebrew and contributors**: kaiHEN, libkaihen, libounce and related runtime components. Original names and notices are preserved for attribution in `engines/iz2s-core/runtime/package/licenses/` and runtime provenance. [kaiHEN](https://github.com/ouncebrew/kaihen).
- **OunceVK, Mesa/NVK/Zink, devkitPro, switchbrew and their contributors**: graphics, native runtime and homebrew components. Individual component licenses apply; bundled texts include Mesa, libounce, libnx, nx-hbloader and nx-hbmenu.
- **Gezine and the CivSwitch/LDN contributors**: Civilization VII integration. See `engines/CivSwitch/LICENSE` and `engines/CivSwitchLDN/LICENSES/`. [CivSwitch](https://github.com/Gezine/CivSwitch).
- **misshexa66 and OunceCake contributors**: filesystem tooling. [OunceCake](https://github.com/misshexa66/OunceCake); original GPL notice retained in `core/filesystem/ouncecake/`.
- **shy / shy1132 and VacuumTube contributors**: the MIT-licensed application underlying VacooTube. Its source copyright and license remain in `modules/VacooTube/app/`. [VacuumTube](https://github.com/shy1132/VacuumTube).
- **hyln9 and nxhid contributors**: the Switch keyboard base. Firmware includes its MIT license, libnx and font notices. [nxhid](https://github.com/hyln9/nxhid).
- **M5Stack, lovyan03, Benoit Blanchon, Ha Thach, Espressif and Arduino contributors**: Cardputer firmware dependencies, including M5Cardputer, M5Unified, M5GFX/LovyanGFX, ArduinoJson, TinyUSB, ESP-IDF, Arduino-ESP32 and IRremote. Versions and texts are retained with the firmware.
- **DejaVu/Bitstream font authors, Monocypher authors, GitHub Octicons authors**, and the authors of Terser, Acorn, source-map, jridgewell mapping libraries and buffer-from: typography, signature verification, artwork and runtime JavaScript processing. Original notices remain beside their components.
- **PCSX2, PCSX-ReARMed and their contributors**, plus VIXL, cubeb, SoundTouch, libchdr, fmt, ImGui, libzip, cpuinfo, simpleini, rcheevos, discord-rpc, plutosvg and plutovg authors: inherited statically linked runtime code. IZPSX is included; associated native dependency license texts are retained in `LICENSES/native-linked/`. Native source coverage is described in [SOURCES.md](SOURCES.md).
- **Electron/Chromium, Node.js, Python, uv, Pillow, OpenSSH, FFmpeg and audio-stack contributors**: bundled or separately installed host dependencies. Electron's complete third-party HTML notice is preserved in each bundled runtime. Downloaded tools retain their own license terms.

`LICENSE` and `COPYRIGHT` cover independently authored IZMYSH code under **GPL-2.0-or-later**. Explicit MIT components (including Foldrift and IZ2S Cheese), third-party code, fonts and prior grants retain their own licenses. This is not a blanket relicensing. See [LICENSE-SCOPE.md](LICENSE-SCOPE.md), [THIRD_PARTY.json](THIRD_PARTY.json), [AUTHORS.md](AUTHORS.md) and [LICENSES/INDEX.md](LICENSES/INDEX.md).

The native runtime contains additional previously linked components, whose notices remain applicable. Their notices must be preserved. See [SOURCES.md](SOURCES.md) for the scope of the available native sources and [COMPATIBILITY.md](COMPATIBILITY.md) for platform support.
