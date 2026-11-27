# IZ2SCiv core - Civilization VII entry for IZ2S

This is the panel's sole entry engine in **IZ2S 0.3.4**, paired with launcher **0.34**, local prototype revision `RUN34-0.34`. Target: Switch 2 firmware **23.0.1**, **Nintendo Switch 1 cartridge of Civilization VII 1.0.0**.

Use the current [English installation guide](../../docs/INSTALLATION.md) or [русскую инструкцию](../../docs/INSTALLATION.ru.md). The panel has four steps: SD card, PC preparation, connection with automatic save installation, and launch. Initial LDN delivery requires native Linux and a compatible Wi-Fi radio.

## Runtime and SD layout

The SD kit puts the following under `SD:/iz2s/`:

| Path | Purpose |
| --- | --- |
| `core/bin/bootstrap.bin` | Native bootstrap |
| `core/bin/ro/main` | RO component |
| `core/bin/core/main` | Core component |
| `core/bin/game/main` | Embedded native menu / game entry |
| `izlauncher.iza` | Launcher application |
| `izlauncher.romfs` | Launcher resources |
| `modules/` | IZ2S Cheese, IZPSX, IZPS2, Foldrift and their companions |
| `licenses/` | Retained component notices |
| `iz2sciv-sd-manifest.json` | File inventory and compatibility metadata |

Bootstrap-generated ExeFS packages are not prerequisites in this SD ZIP. There is no separate Homebrew Menu package in this edition. All files live under `iz2s/`; preserve Nintendo and the user's other files when merging an update.

The builder validates full runtime sizes and SHA-256 using `../iz2s-core/runtime/package-manifest.json`. Native input/output bindings remain in `menu-build.json`. The RUN33 `iz2s.sd-flex.v1` save strictly pins only `bootstrap.bin`, `ro/main` and `core/main`. Compatible launcher, menu and ROMFS updates no longer require reinstalling the save. Existing exact-size saves require one policy upgrade. Boot core or ABI changes still require a matching save.

Console preflight checks mutable NSO/IZA/ROMFS headers and file extents, reading 848 bytes total. This is structural validation, not a full content hash or proof that arbitrary builds can be mixed. Complete file hashes remain checked on the PC, and native IZA CRC checks are retained. There is no skip-check flag. The session retains a full build fingerprint to reject changes during installation, but uses the boot core and policy fingerprint to decide whether an installed save needs updating.

## Save installation and connection

**Connect and install save** performs LDN delivery, restores PC Wi-Fi, verifies the game loader and automatically installs `IZ2SCIV.Civ7Save`. Only that trigger save is replaced; the filesystem commit must confirm success. Conditional deletion of the whole profile's game save data during migration is a separate, explicit console step.

The console IP controls are directly below session status. A candidate address or open FTP port is not accepted as proof of a compatible loader. Leave Awaiting payload open while installing. Preview allows local navigation and SD downloads but disables console actions.

After installation, return to HOME, close Civilization VII with X, reopen it and choose Load Game without rebooting. Follow the panel through IZ2S CORE IS RUNNING and the R-held game launch. PC Link is R + L3; FTP is L + R3.

## Command-line alternatives

Run these from this engine's directory:

```sh
./iz2sciv.sh prepare-sd
./iz2sciv.sh build
./iz2sciv.sh check
```

These are local operations. The SD output is `dist/iz2sciv-core-sd.zip`. Use the PC panel for initial wireless setup; the old `setup` CLI alias now explains this instead of relying on a developer-specific key path and an unshipped local environment. For an intentionally prepared console session, `./iz2sciv.sh install --host IPv4` installs through an already verified loader; `./iz2sciv.sh launch --host IPv4` sends a one-time launch.

Historical helper names such as `src/kaihen/` and compatibility aliases preserve upstream provenance and ABI relationships. They do not select another runtime or expose an SB4 setup route. Local build, ABI and archive checks do not prove an on-console boot.

## License

CivSwitchFS is MIT. Code ported from ns2jb and kaiHEN retains GPL v2.0 terms.
See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Acknowledgments

Thanks to Nerisma (CivSwitchFS), ave (the kaiHEN port), Gezine (CivSwitch), Slapah/Hokage (the CivSwitch filesystem port), pepsipu (ns2jb), Kurumuz (kaiHEN and the ns2jb fork), Alula (stability improvements), the civswitch-kit and civ7-fs contributors, epiccsolar (independent kaiHEN work), and IZMYSH (RUN25 integration). [CivSwitchFS](https://github.com/ItsNerisma/CivSwitchFS), [CivSwitch](https://github.com/Gezine/CivSwitch), [Slapah/Hokage attribution](https://github.com/byizmysh/IZCIVSwitch/blob/main/NOTICE.md#upstream), [ns2jb](https://gitlab.com/pepsipu/ns2jb), [Kurumuz’s ns2jb fork](https://gitlab.com/kurumuz/ns2jb), [civswitch-kit](https://gitee.com/mza1010/civswitch-kit), [civ7-fs](https://github.com/HerXayah/civ7-fs).

LLMs were involved in the development of this. Please don't use it if you don't trust it.
