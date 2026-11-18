# Platform support in 0.25

| Component | Linux x64 / ARM64 | Windows x64 |
| --- | --- | --- |
| Local panel and application packages | Included | Included |
| Desktop / application-menu shortcuts with icon | Included | Included; no console window |
| Input-device picker | Switch 1, Cardputer ADV, ARM PC | Switch 1, Cardputer ADV, ARM PC |
| Switch 1 setup | SD package; DBI where supported | SD package; extract at SD root |
| BlitBun and VacooTube PC media hosts | Supported | Linux hosts required |
| Civilization VII Wi-Fi setup | Linux networking tools required | Not supported |

The panel is normally at `http://127.0.0.1:39524`; PC Link uses port `39525`. Firewall and local-network reachability still apply. Device discovery can fall back to an explicitly entered address. Physical console input from Windows remains unverified. Windows ARM64 and 32-bit installations are not supported.

The console FTP default is **1425**. Follow the address and port shown by the active console menu when an older runtime is installed. Console runtime changes take effect after reopening the game/menu as directed by the panel.

Six bundled applications are listed in [README.md](README.md#included-applications). Their versions are independent of the Platform version. BIOS, games, keys and existing user sessions are not bundled.
