# IZ2S Platform 0.22.0

**Civilization VII support arrives in IZ2S Platform.** Version 0.22 also brings autonomous IZlauncher operation after startup, PC connection recovery, and expanded filesystem tools for installing apps and managing console files.

## New: Civilization VII support

Civilization VII is now a second entry point alongside SmileBASIC 4. Two First Setup methods are available:

- **IZ2S core Wi-Fi** — initial delivery over local wireless (LDN) from a Linux PC with a supported Wi-Fi adapter. No Cardputer ADV or ARM input device is needed. Setup requires your own compatible `prod.keys` and the helper permissions shown in the panel.
- **USB** — automatic input through **Cardputer ADV** or an **ARM Linux device**, using the panel's guided setup.

After installing the loader save, later launches are simpler: open **Civilization VII → Load Game**, wait for **Awaiting payload**, and press **Connect and start launcher** in the panel. You can use **LAN or Wi-Fi** for these connections; the initial delivery does not need to be repeated. Choose IZlauncher, IZPSlauncher, or Core only.

The supported starting configuration is the **Switch 1 cartridge version of Civilization VII 1.0.0**. Installing the loader save replaces single-player saves, so follow the backup instructions before proceeding.

## Autonomous IZlauncher

**Once started, IZlauncher works autonomously on the console.** Browse the library and launch compatible local SD applications without keeping the PC connected. Applications installed on SD remain available between sessions.

The initial launcher startup still uses the PC. Streaming applications and PC-side services require a connection; autonomous operation does not make those services available offline.

IZlauncher also has a redesigned interface: app tiles, clear **SD/RAM** badges, recent activity, animated selection effects, and a gently animated background. IZPSlauncher remains available as an alternative.

## Restore the PC connection

Use **Restore connection** to reconnect the panel to an existing launcher session. Return to app management and file operations while keeping the running launcher and its loaded applications, without starting a new session from scratch. Recovery requires the console session and its recovery service to remain available.

## Expanded filesystem access

**Super File Bros.** is integrated into the panel as **Files & uploads**: browse console storage, upload games and applications to SD, create folders, and manage files from the browser. Optional FTP access is available for external file managers.

- Visible transfer progress and cancellation controls.
- Interrupted uploads can continue from a verified checkpoint when the same original file is selected and the current session supports resuming.
- App and module file views share the same file service: IZPSX does not require a separate Super File Bros. window.
- The App library supports RAM/SD installation, installed/not-installed filters, and location filters. RAM loading requires an explicit selection.

Filesystem access must be acquired for features that need it. SmileBASIC acquisition can still fail; follow the panel's recovery instructions after an unsuccessful attempt.

## Included applications

| Application | Purpose |
| --- | --- |
| **File Exchange** | Title-owned file access and FTP |
| **File Exchange FS** | Extended filesystem access; works with File Exchange |
| **Book Reader** | TXT and unencrypted EPUB reading |
| **Super File Bros.** | Integrated browser file manager and uploads |
| **IZPSX emulator 0.5.1** | PlayStation games with local SD game/BIOS access, persistent memory cards, and per-player control settings |

**IZPSX emulator requires IZ2S Panel and filesystem access.** Plus acts as PS1 Start; Minus opens the emulator menu. Its corresponding source and licenses are included.

## More improvements

- Fixed native SD launcher startup after a shared-interface update; recovery and reader resources now rebuild together, with mismatches caught before console startup.
- Clearer connection status, consistent controls, and responsive session/file-manager cards.
- Guided Civ7 setup with Stop and Start over actions.
- Improved attempt history, filtering, and handling of resolved errors.
- Updated documentation, ADV/ARM setup guides, network preparation advice, and new images of the panel and both launchers.

[Installation and full guide](https://github.com/byizmysh/IZ2S-Platform/blob/v0.22.0/README.md) · [FAQ](https://github.com/byizmysh/IZ2S-Platform/blob/v0.22.0/FAQ.md) · [Compatibility](https://github.com/byizmysh/IZ2S-Platform/blob/v0.22.0/COMPATIBILITY.md) · [IZPSX guide](https://github.com/byizmysh/IZ2S-Platform/blob/v0.22.0/sources/izpsx/README.md)
