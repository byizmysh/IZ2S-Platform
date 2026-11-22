# IZ2S Platform 0.3.0

## Downloads

- `IZ2S-Platform-0.3.0-linux.tar.gz` — Linux x64 / ARM64.
- `IZ2S-Platform-0.3.0-windows-x64.zip` — Windows x64.
- `SHA256SUMS.txt` — checksums for both installation archives.

Use these installation assets; GitHub's automatic Source code archives are for development.

## Installation

Extract the entire archive into a new folder. Linux: `sh ./install.sh --launch`. Windows: double-click `Start.cmd`; missing dependencies are prepared automatically and later launches reuse them. Use `Start.vbs` to launch without a console window after setup. See [README.md](README.md).

## Changes

- SmileBASIC 4 launch route on Linux and Windows; Windows uses a bundled Node.js bootstrap and automatic private dependency setup.
- Civilization VII integrated filesystem engine on Linux. Windows explicitly labels its tile **Linux Only**.
- Windows SB4 process cleanup supports stopping and restarting the listener.
- Saved Cardputer ADV and Switch 1 pairings can be restored across installations for the same user; new-device setup creates pairing automatically.
- Six application packages retained; IZ2S Cheese is 0.8.1.
- Original CivSwitch, CivSwitchLDN, CivSwitchFS, kaiHEN-port and LDN authors are credited alongside the existing acknowledgements, with their original license texts preserved.

## Applications and scope

BlitBun 1.15.8, Book Reader 0.1.0, Foldrift 0.3.3, IZ2S Cheese 0.8.1, VacooTube 1.5.7, IZPSX 0.5.2. BlitBun and VacooTube PC media hosts require Linux. BIOS, games and personal state are not included.

See [COMPATIBILITY.md](COMPATIBILITY.md) for testing scope, [AUTHORS.md](AUTHORS.md) for credits, and [SOURCES.md](SOURCES.md) for available sources and remaining native rebuild limitations.
