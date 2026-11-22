# Platform support in 0.3.0

| Game / entry point | Linux x64 / ARM64 | Windows x64 |
| --- | --- | --- |
| SmileBASIC 4 (SB4) | ✅ Supported | ✅ Supported |
| Civilization VII | ✅ Supported | ❌ **Linux Only** |

| Component | Linux x64 / ARM64 | Windows x64 |
| --- | --- | --- |
| Local panel and application packages | Included | Included |
| Startup | Per-user installation; desktop/application-menu shortcuts | Portable folder; automatic first-run setup; Start.vbs for later launches |
| Input-device picker | Switch 1, Cardputer ADV, ARM PC | Switch 1, Cardputer ADV, ARM PC |
| Switch 1 setup | SD package; DBI where supported | SD package; extract at SD root |
| BlitBun and VacooTube PC media hosts | Supported | Linux hosts required |
| Civilization VII wireless lobby connection | Compatible adapter and Linux networking tools required | Not supported; tile marked Linux Only |

The panel is normally at `http://127.0.0.1:39524`; PC Link uses port `39525`. Firewall and local-network reachability still apply. Device discovery can fall back to an explicitly entered address. Pairing is generated through Device setup; no manually obtained keys are required. Windows ARM64 and 32-bit installations are not supported.

Windows 11 virtual-machine checks covered automatic setup, dependency reuse, panel navigation, SB4 listener startup/stop/restart, and authenticated status from Cardputer ADV and Switch 1. They did not complete a full physical-console SB4 session. The Windows SSH client was checked; a connection to a physical ARM host was not tested in this round. The support table describes the available launch routes, not a claim that every hardware configuration has been tested.

The console FTP default is **1425**. Follow the address and port shown by the active console menu when an older runtime is installed. Console runtime changes take effect after reopening the game/menu as directed by the panel.

Six bundled applications are listed in [README.md](README.md#included-applications). Their versions are independent of the Platform version. BIOS, games and existing user sessions are not bundled.
