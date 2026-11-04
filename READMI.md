# IZ2S Platform 0.21 — Quick guide

See the [main README](README.md) for an overview, [FAQ.md](FAQ.md) for detailed explanations and [COMPATIBILITY.md](COMPATIBILITY.md) for platform status.

## Install and start

Extract the complete archive first. Do not run an entry file from inside a ZIP or move it away from the other files.

| OS | Open once to install | Open later |
| --- | --- | --- |
| Linux x64 / ARM64 | `Install-Linux.desktop`; allow launching if requested | IZ2S Platform shortcut or installed `start.sh` |
| Windows x64 | `Install.cmd` | Start-menu shortcut or installed `Start.cmd` |
| macOS Intel / Apple silicon | `Install.command` | `~/Applications/IZ2S Platform.app` or installed `Start.command` |

If Linux does not execute desktop files, open a terminal in the extracted folder and run `sh install.sh --launch`. Linux package installation may request sudo; Arch may ask to synchronize/upgrade the system. Windows/macOS download private Node.js 24, Python 3.12 and Pillow. Windows also supplies a portable OpenSSH client if its existing client is missing. No global npm packages, Homebrew or Xcode are required. Windows/macOS setup needs internet access and a normal writable user profile. The installer prints the new installation folder and starts the panel in a browser window.

Windows may show a downloaded-file warning. Only approve a package whose source you trust; organization policy can still block scripts. On macOS, review the specific file in **System Settings → Privacy & Security → Open Anyway** if offered. Do not disable OS protection globally. If a `.command` file lost execute permission, run `chmod +x Install.command Start.command` from Terminal in that folder. Details and permission/network recovery are in the [FAQ](FAQ.md#installation-and-os-prompts).

**Windows/macOS are experimental installer targets. Their setup, browser launch, firewall behavior and complete device/session workflow require OS testing. Native console session descriptors remain Linux-only in this candidate.** Opening the panel is possible without claiming console support. See the table before planning a console test. On runtime minimums: Windows 10+ x64, or macOS 13.5+, 64-bit x64/ARM64; these minimums do not qualify IZ2S itself. See the [local verification record](distribution/LOCAL-CHECKS.md) for the current test scope.

## Names and included applications

- **IZ2S Platform:** the PC control panel.
- **IZlauncher / IZPSlauncher:** the two console menus, version 0.21.
- **[IZ2S Core](engines/izmysh/README.md):** default connection engine; developer IZMYSH.
- **[ns2jb by BermTheSequel](engines/BermTheSequel):** bundled alternative.
- **[Super File Bros.](https://github.com/byizmysh/super-file-bros):** integrated file-browser tab; its standalone distribution remains independent.
- **[File Exchange](modules/iz-exchange/README.md), [File Exchange FS](modules/iz-exchange-fs/README.md) and [Book Reader](modules/BookReader/README.md):** bundled modules.

[Emulators and other modules](modules/README.md#additional-modules), [VacooTube](https://github.com/byizmysh/VacooTube) and [BlitBun](https://github.com/byizmysh/BlitBun) are separate downloads with their own requirements.

## First Setup and Launch

Choose the console menu, connection core and input device. Run **First Setup**, follow its screen prompts and save the BASIC program. Fully restart Switch 2, open fresh SmileBASIC Direct, then use **Launch**. The configured console target is Switch 2 system 23.0.1 / SmileBASIC 4.4.9. An ended or failed native session requires another full console restart.

For **[ARM PC](hardware/agents/arm-input/README.md)**, the separate Linux input board needs SSH and a working USB keyboard gadget. Deploy the supplied `packages/iz2s-arm-input.tar.gz` through First Setup. Compatible input API 1 agents remain usable across launcher updates. Installing the PC application does not install or repair the board OS.

For **[Cardputer ADV](hardware/devices/cardputer-adv/README.txt)**, follow the firmware step using M5Launcher USB storage or download **File** and copy its package manually. Launch the firmware, select the PC's local Wi-Fi and enter its password with the ADV keyboard; Tab shows/hides the password. Enter the IP shown on ADV in Device settings if automatic `iz2s-adv.local` naming is unavailable. Then move its USB data cable from the PC to Switch 2 and confirm the connection. Text/actions are preloaded; ADV types locally. Match the panel's Caps Lock setting to SmileBASIC to preserve intended capitalization.

## Files and modules

Enable **FS endpoint (--rpc)** before Launch. Retained FS access opens Super File Bros. inside IZ2S; an external file manager is an additional choice. FTP exports and internal storage are read-only. Browser controls support explicit SD changes. File Exchange's `/save`, `/tmp` and `/rom` belong to the SmileBASIC title, not the general SD or game exports.

Modules share `SD:/IZ2S/Homebrew/Modules/<module.id>/`. IZ2S prepares these folders when SD storage opens; modules must explicitly adopt the path. Existing files are not migrated. The [SDK guide](sdk/SD-STORAGE.md) documents `context.sdStorage`.

Closing a module tab leaves its service running. Use sidebar **Stop** or **Stop module**; reopen its view from the sidebar. End a console session with its own Stop action. A transport loss or uncertain native cleanup is not automatically replayed; restart the console before trying again.

## Updating from 0.16

Install into a new folder and keep the old installation as a backup. The 0.16 default engine used `NS2I`, port 39395 and nine START arguments; 0.21 uses `NS2IZM`, port 39380 and five arguments. Run First Setup for the new core; renaming the old program is insufficient. Re-enter device/network choices, preserve private pairing/SSH material deliberately and check each extra module's OS requirements. Do not copy old runtime/session snapshots or overwrite the new engine files. See [migration details](FAQ.md#updating-from-016) and the full [release notes](RELEASE-NOTES.md).

Builds and installers do not publish anything.
