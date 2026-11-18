# IZ2S Platform 0.25

IZ2S is a Homebrew launcher for Nintendo Switch.

## Downloads

| System | File |
| --- | --- |
| Linux x64 / ARM64 | [IZ2S-Platform-0.25-linux.tar.gz](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.0/IZ2S-Platform-0.25-linux.tar.gz) |
| Windows x64 | [IZ2S-Platform-0.25-windows-x64.zip](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.0/IZ2S-Platform-0.25-windows-x64.zip) |

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

## Changes

- Separate Linux and Windows installation packages with automatic panel startup.
- Updated device selection for Switch 1, Cardputer ADV and ARM PC.
- Windows Switch 1 setup uses an SD package with IZ2S SB4 Keyboard and pairing configuration.
- Automatic network discovery, console file tools, and the integrated application library.
- IZ2S Cheese replaces the previous Store name; Foldrift provides the SD file manager.
- Console FTP uses **port 1425 by default**.

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

[Full installation guide](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.0/README.md) · [Authors and licenses](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.0/AUTHORS.md) · [Source information](https://github.com/byizmysh/IZ2S-Platform/blob/v0.25.0/SOURCES.md) · [SHA-256 checksums](https://github.com/byizmysh/IZ2S-Platform/releases/download/v0.25.0/SHA256SUMS.txt)
