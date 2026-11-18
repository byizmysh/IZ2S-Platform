# IZ2S Platform 0.25.1

IZ2S is a Homebrew launcher for Nintendo Switch.

## Downloads

| System | File |
| --- | --- |
| Linux x64 / ARM64 | [IZ2S-Platform-0.25.1-linux.tar.gz](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.1/IZ2S-Platform-0.25.1-linux.tar.gz) |
| Windows x64 | [IZ2S-Platform-0.25.1-windows-x64.zip](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.1/IZ2S-Platform-0.25.1-windows-x64.zip) |

Extract the entire archive before installation. Use these installation packages rather than GitHub's automatic **Source code** downloads.

## Installation

**Linux:** open a terminal in the extracted folder and run:

```sh
sh ./install.sh --launch
```

**Windows:** extract the ZIP with **Extract All**, then double-click:

```text
Install.cmd
```

The installer prepares the required PC dependencies, creates shortcuts with the IZ2S icon and opens the panel. For later launches, use the shortcut; Windows also provides `Start.vbs` for startup without a console window.

## Fixes in 0.25.1

- FIX: Automatically choose the PC network address from the Switch 2 LAN/Wi-Fi path. A Switch 1 keyboard on Wi-Fi no longer redirects a LAN launch to the PC's Wi-Fi address.
- Added **Restart** after a failed, interrupted or stopped First Setup attempt. It returns to Preparation, clears the form confirmations and keeps the previous attempt in the log history.
- First Setup waits for USB keyboard readiness from Switch 1, Cardputer ADV or ARM PC before enabling source entry.
- A manually confirmed USB cable shows a waiting state until the device reports that its keyboard is ready.
- If the keyboard rejects input before typing begins, the panel shows **Input did not start** and a specific USB message in the selected language.
- Added a specific diagnostic for local IPC socket errors reported as `listen EACCES ... .sock`.
- Added the **Make a Program** navigation hint to First Setup in English, Russian, Spanish and Chinese.

## Included applications

| Application | Version | Restrictions |
| --- | --- | --- |
| [BlitBun](https://github.com/byizmysh/BlitBun) | 1.15.8 | Only Linux |
| Book Reader | 0.1.0 | |
| Foldrift | 0.3.3 | |
| [IZ2S Cheese](https://github.com/byizmysh/iz2s-cheese) | 0.8.0 | |
| [VacooTube](https://github.com/byizmysh/VacooTube) | 1.5.7 | Only Linux |
| [IZPSX emulator](https://github.com/byizmysh/IZPSX-emulator) | 0.5.2 | |

Install applications through **App library**. BIOS and game images are not included. Linux is required for the BlitBun/VacooTube PC media services and Civilization VII Wi-Fi setup.

[Full installation guide](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.1/README.md) · [Authors and licenses](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.1/AUTHORS.md) · [Source information](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.1/SOURCES.md) · [SHA-256 checksums](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.1/SHA256SUMS.txt)
