# IZ2S Platform 0.22

IZ2S Platform is a local PC control panel and console launcher system for Switch 2. Choose **SmileBASIC 4** or **Civilization VII**, connect the console, and manage applications in **IZlauncher** or **IZPSlauncher**. **IZ2S Core** is the default connection engine.

Support for **Balatro** will be added within a week after the developer receives a physical copy. Balatro is not included in this release.

This is a **locally prepared 0.22.0 release candidate**. Nothing is uploaded or published by the build tools. Release targets are **Linux x64/ARM64 only**. Windows and macOS packages are not included. A supplied archive is not a hardware qualification claim. See [Compatibility](COMPATIBILITY.md).

[Screenshots](#screenshots) · [Recommended setup](#recommended-console-and-network-setup) · [Install](#installation) · [Input devices](#input-devices) · [Additional modules](#additional-modules) · [SmileBASIC](#smilebasic-4) · [Civilization VII](#civilization-vii) · [Bundled apps](#included-applications) · [FAQ](FAQ.md) · [Release notes](RELEASE-NOTES.md) · [Support](SUPPORT.md)

## Screenshots

New images of the 0.22 interface. The PC panel is captured in an isolated preview with example connection/installation data. Console-menu images are rendered from the shipped 0.22 shaders and bundled app artwork, with example activity; they are not photographs of a running console. See [image provenance](docs/SCREENSHOTS.md).

### PC control panel

| Connection | App library |
| --- | --- |
| [![IZ2S Platform 0.22 connection page](docs/images/connection.png)](docs/images/connection.png) | [![IZ2S Platform 0.22 App library](docs/images/app-library.png)](docs/images/app-library.png) |
| Connection status, launcher actions, files and uploads, and console inventory. | The bundled apps, RAM/SD installation choices, status and location filters. |

### Console menus

| IZlauncher | IZPSlauncher |
| --- | --- |
| [![IZlauncher 0.22](docs/images/izlauncher.png)](docs/images/izlauncher.png) | [![IZPSlauncher 0.22](docs/images/izpslauncher.png)](docs/images/izpslauncher.png) |
| App tiles, selected app and recent activity over the animated background. | Alternative console library and navigation using the same bundled applications. |

## Recommended console and network setup

Prepare these before First Setup and a fresh native launch:

1. Check the [supported game/system configuration](COMPATIBILITY.md). For SmileBASIC FS acquisition, start in **handheld mode** and use **wired LAN** where available. These are setup recommendations, not a guarantee of acquisition.
2. Keep the PC and console on the same reachable local network. For automatic input, the PC must also reach the chosen ADV/ARM device. Guest Wi-Fi or client isolation can prevent local devices from communicating even when all show internet access. Allow the application's required local traffic in the PC firewall; do not disable the firewall wholesale.
3. Restrict the console's internet access while preserving local communication. [SWFence](https://github.com/byizmysh/SWFence) provides a LAN-only policy on a compatible OpenWrt router. [90DNS](https://gitlab.com/a/90dns) filters Nintendo domains; **it does not block all internet traffic**. Follow each project's current instructions. With SWFence, use local IPv4 addresses when hostname/mDNS discovery is unavailable.
4. Keep the input device's **USB data cable** connected to the console/dock as required by its guide. A charging cable is insufficient. Wi-Fi reachability alone does not prove USB keyboard/controller readiness.
5. On a fresh SmileBASIC launch, open **Make a Program → Direct**, use the expected English/US keyboard layout and match Caps Lock in the panel. For Civ7, follow the wizard's lobby/chat or Load Game screen for the selected stage instead of entering a BASIC Direct screen.

For repeated SmileBASIC FS failures, stop the attempt and fully restart Switch 2. The recovery workaround is to open an installed game for about **10 seconds**, close it completely, then open **SmileBASIC 4 → Make a Program → Direct** and try Launch again. It is not guaranteed; do not retry over an uncertain retained native session. [FS and network FAQ](FAQ.md#recommended-console-and-network-setup).

## Installation

Extract the whole archive into a writable folder. Keep its files together; do not run an entry point from inside a ZIP.

| Host | Install | Start later |
| --- | --- | --- |
| Linux x64 / ARM64 | `Install-Linux.desktop`, or `sh install.sh --launch` | Application shortcut or `start.sh` |

Linux supports apt, pacman and dnf dependency installation; initial setup needs internet access. The installer prepares the PC application and does not start a console session, flash a device or install a board OS.

Use [host acceptance steps](distribution/HOST-TESTING.md) to qualify an actual machine. Native features remain subject to each component's host declaration; this release does not enable or qualify other host systems.

## Input devices

**Cardputer ADV and ARM PC are alternatives; you do not need both.** They provide automatic USB input for SmileBASIC and Civ7 USB delivery. Civ7 **IZ2S core Wi-Fi** First Setup uses the PC Wi-Fi adapter directly and needs neither device. After a supported session connects, ordinary launcher/file traffic uses LAN/Wi-Fi.

### Cardputer ADV

1. Choose **Cardputer ADV** in the panel and open **Setup and firmware**. Use the package offered for the selected game. The release keeps the SmileBASIC profile at **0.21.3** and supplies the Civ7/default composite firmware at **0.22.4**; do not select a firmware just because its number is higher.
2. Connect ADV to the PC with a data cable and enable **USB storage** in M5Launcher. Let the panel copy only to a positively identified ADV card, or download the **File** package and extract it to the card root. Keep M5Launcher, other firmware and existing Wi-Fi configuration. The package carries its pairing file; keep it private.
3. Eject storage normally. Use **SD → Utilities** in M5Launcher and run the application image supplied by the package. This is an M5Launcher application, not an address-zero bootloader image; use the offered filename rather than renaming/flashing it manually.
4. Join the local Wi-Fi that can reach the PC. Select the access point on ADV, type its password and press Enter. **Tab** shows/hides the password. If `iz2s-adv.local` is unavailable, enter the IPv4 address displayed on ADV in the panel's device settings. A plain IP is accepted.
5. Move the ADV **USB data cable from the PC to Switch 2/dock**. The PC continues controlling ADV over Wi-Fi. The panel checks USB state over that link; complete **Confirm connection** if offered. A manual cable confirmation records your cable choice and does not replace measured keyboard/controller readiness.
6. Match the panel's **Caps Lock** setting to the console. Wait for the required USB interfaces to become ready, then follow First Setup or the fresh Launch screen. Use panel **Stop**, or the documented ADV stop control, to interrupt input. Accepted input is not automatically replayed after losing a receipt or restarting ADV.

The composite Civ7 profile provides keyboard and controller input; the SmileBASIC profile uses its supplied keyboard workflow. Idle USB recovery must not replay source text or reset active typing. A charge-only cable, unavailable dock data port or guest-network isolation needs the corresponding cable/host/network fix. See the [full ADV firmware, Wi-Fi, USB and recovery guide](hardware/devices/cardputer-adv/README.txt), [M5Stack device documentation](https://docs.m5stack.com/en/core/Cardputer-Adv) and [M5Launcher application guide](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project).

### ARM PC

1. Prepare the board's own Linux OS first: Python 3, systemd, SSH and a working **USB device/OTG controller** with configfs/libcomposite/HID support. A USB-C connector alone is not evidence of gadget support. The IZ2S package does not install a board OS or supply a missing USB driver.
2. Connect the PC and board over LAN. Verify the board's SSH host fingerprint through its own console, then enter its IPv4 address, user and optional private-key path in the panel's **Device settings**. Keep password entry in the terminal; the panel never uploads your private key.
3. Choose **Install package** for that board, or download the self-contained ARM installer for manual/password-only setup. Civ7 and SmileBASIC installer names contain the same input agent; use the file offered by the panel. Existing input API 1 installations do not need replacing for every launcher update.
4. For manual installation, copy the downloaded installer to the board, connect by SSH and run it there with Python 3 and the necessary privileges. `--check` inspects the board; `--check-package` validates the embedded package without installing. Do not run it on the server PC unless that PC is itself the intended USB input board.
5. Connect the board's **USB data/OTG port to Switch 2/dock**. Check the keyboard interface for SmileBASIC and both keyboard/controller interfaces for Civ7 automatic input. A successful SSH connection does not prove that Switch has configured USB input.
6. Start input from the panel only on the requested game screen. Stop releases the owned input interfaces; losing an SSH receipt is not permission to enqueue the same source again. Leave unrelated board USB gadgets and services intact.

See the [full ARM setup, manual installation, checks and troubleshooting guide](hardware/agents/arm-input/README.md). The PC-to-ARM LAN link and ARM-to-console USB link are separate; a Civilization VII wireless lobby may temporarily pause the console's IP network without breaking PC-to-ARM SSH.

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

## Additional modules

These are separate downloads, **not part of the five-app/tool bundle above**. Install IZ2S Platform first, then follow the selected module's own instructions and host/dependency requirements.

| Module | What it does | Useful features | Download and setup |
| --- | --- | --- | --- |
| **[BlitBun](https://github.com/byizmysh/BlitBun)** | Streams a PC application's video and audio to Switch 2. | Live output-resolution changes, separate Dock/Handheld profiles and optional forwarding of controllers paired with the console to the PC. Streaming needs a running PC application and a usable local connection; FPS options are targets, not guaranteed rates. | [Latest release](https://github.com/byizmysh/BlitBun/releases/latest) · [Install and configure](https://github.com/byizmysh/BlitBun#readme) |
| **[VacooTube](https://github.com/byizmysh/VacooTube)** | YouTube player controlled from Switch 2 through a TV-style interface; its application runs on the PC. | Console touch/controller navigation, search and playback, live interface-resolution settings and its own bundled BlitBun streaming engine. A separate BlitBun installation is not required; the PC/network and online video service remain necessary. | [Latest release](https://github.com/byizmysh/VacooTube/releases/latest) · [Install and configure](https://github.com/byizmysh/VacooTube#readme) |

Both packages carry their own PC/console integration and can be installed independently. Neither module requires the other. Use a release compatible with your platform/native ABI; downloading a package does not install its host dependencies or update an already running console session. Other compatible modules are described in [the module guide](modules/README.md).

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
