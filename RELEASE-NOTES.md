# IZ2S Platform 0.25.2

IZ2S is a Homebrew launcher for Nintendo Switch.

## Downloads

| System | File |
| --- | --- |
| Linux x64 / ARM64 | [IZ2S-Platform-0.25.2-linux.tar.gz](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.2/IZ2S-Platform-0.25.2-linux.tar.gz) |
| Windows x64 | [IZ2S-Platform-0.25.2-windows-x64.zip](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.2/IZ2S-Platform-0.25.2-windows-x64.zip) |

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

## Fixes in 0.25.2

- FIX: Open FTP from the console with **L + R3** and PC Link with **R + L3**. Each channel can be closed and reopened independently after a network change.
- FIX: Remove saved pairing profiles and passwords from these two console channels. The selected channel accepts connections while it is open.
- FIX: Discover the current console address across LAN and Wi-Fi and reconnect after an interface change.
- FIX: Allow `.iza` modules to launch across platform and launcher updates when their required runtime functions are available, without matching application version numbers.
- FIX: Restore missing native functions used by IZ2S Cheese 0.8.0 and keep the app's compatibility fallback.
- FIX: Show the correct `0.25.2` release number in the panel health response and component metadata.

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

[Full installation guide](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.2/README.md) · [Authors and licenses](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.2/AUTHORS.md) · [Source information](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.2/SOURCES.md) · [SHA-256 checksums](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.2/SHA256SUMS.txt)
