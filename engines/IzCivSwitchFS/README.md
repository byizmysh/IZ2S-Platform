# IZ2SCiv core

IZ2S Platform's Civilization VII entry for **IZ2s**. Sources live in
`engines/IzCivSwitchFS`. IZ2S Platform selects this engine under **Connection → Civilization VII → IZ2SCiv core**.
This is the only selectable Civilization VII engine. The former USB and Wi-Fi
engine choices are migrated to it; their source folders remain internal LDN
transport dependencies. SmileBASIC 4 keeps its existing engine.

In IZ2S Platform, choose **First Setup** and follow the numbered buttons:
1. Click **Download SD archive**, extract the ZIP to the **SD card root** and
   put the card back into the powered-off console. Confirm **Files copied — continue**.
   The archive contains the complete required runtime; no SB4 or FTP setup is needed.
2. Follow the single highlighted action on **PC setup**: choose your own
   `prod.keys`, prepare missing PC components, then **Allow Wi-Fi access**
   (Linux opens an administrator prompt). Existing keys show **Saved and verified**;
   ready components are skipped. One Wi-Fi adapter is selected automatically;
   choose an adapter when several are available. When all three checks are ready,
   click **Continue to connection**. This only opens step 3 and sends nothing.
   The fresh-lobby checkbox enables **Connect and continue**; only that button
   starts delivery. Consent is cleared when returning to step 3.
3. Reboot the console, open a fresh Civ7 wireless lobby, confirm it and click
   **Connect and continue**. IZ2S Platform sends the loader over a temporary direct
   Wi-Fi interface, restores the previous Wi-Fi profile, waits for its IPv4
   address, then checks the console. A selected IP is retained for that check.
   If `Awaiting payload` is already open, keep it open and click **Check connection and continue** instead. Successful connection opens step 4 automatically; do not reboot between these steps.
4. Confirm **I want to install IZ2SCIV.Civ7Save.** and click **Install launch save**.
   Only `save:/saves/Single/IZ2SCIV.Civ7Save` is written or replaced; no backup
   files are created. A temporary file is fully written before replacing the target.
   Other game saves are kept. Success requires a confirmed filesystem commit;
   if commit fails, installation is unconfirmed and must be retried.
5. Follow **Launch**: restart Civ7 → Load Game → wait for `IZ2S CORE IS RUNNING` →
   close Civ7 → hold R while opening a Switch 2 game (repeat once if needed).
   Enable **PC Link** in IZ2s with **R + L3** to use the app library.
   For file access only, enable FTP with **L + R3** and open **Super File Bros**.

The panel restores the current setup step after reload. Connection completion,
installation confirmation, transfer progress and confirmed installation are separate states.
It never treats an earlier installation record as completion of a new connection attempt.
Progress, errors, Stop and **Restore PC Wi-Fi** appear in one status card directly
below the engine tile, on every step. A recovery record is saved before releasing
the selected radio and retained if recovery fails. An interrupted attempt is
recovered before the next lobby transfer; Ethernet and other radios are untouched. The single session log switches
between **Key events** and **Full output**; the download exports the latest attempt.
Preview permits navigation and the local SD archive download; console actions are disabled.
The command-line entry points below remain available as alternatives.

The chain acquires filesystem access through CivSwitchFS, then starts the
**same IZ2S native runtime and launcher used by SmileBASIC 4**. It does not
load a kaiHEN release, `/kaihen/payload.js`, or `/hbmenu.nro`.

## SD archive layout

| File | Use |
| --- | --- |
| `/iz2s/core/bin/bootstrap.bin` | IZ2S native bootstrap |
| `/iz2s/core/bin/ro/main` | IZ2S RO component |
| `/iz2s/core/bin/core/main` | IZ2S core |
| `/iz2s/core/bin/core/exefs.nsp` | Created by native bootstrap on the console; not a prerequisite |
| `/iz2s/core/bin/game/main` | IZ2S game loader |
| `/iz2s/izlauncher.iza` | IZ2s |
| `/iz2s/izlauncher.romfs` | Launcher resources |
| `/iz2s/modules` | Existing applications; retained |

The build verifies the local runtime against
`../iz2s-core/runtime/package-manifest.json`. Before launch, the console
checks all required file sizes and SHA-256 for bootstrap, RO and core.
Missing or mismatched files stop the chain before the native launch.
There is no fallback to kaiHEN and no release relocation or skipcheck flag.

## First Setup

Requirements: Switch 2 firmware **23.0.1**, **Civilization VII 1.0.0**,
the supplied SD archive extracted to microSD, and the established isolated local network.

1. Download **iz2sciv-core-sd.zip** from step 1 in IZ2S Platform. Alternatively build it locally:
   ```sh
   ./iz2sciv.sh prepare-sd
   ```
   The ZIP appears in `dist/iz2sciv-core-sd.zip`. Extract it directly to the SD
   root: `SD:/iz2s/core/bin/...`, `SD:/iz2s/izlauncher.iza` and
   `SD:/iz2s/izlauncher.romfs`. Do not add an enclosing archive folder.
   Merge existing folders and replace matching archive files; keep other apps.
   Native bootstrap creates its ExeFS packages from the console policy on launch.
2. Safely eject the card, put it into the powered-off console, then start Civ7
   and open the wireless lobby for initial loader setup.
3. Run the complete Wi-Fi setup sequence:
   ```sh
   ./iz2sciv.sh setup
   ```
   The existing local LDN setup runs first, restores the PC network, then the
   launcher discovers the console on TCP 9026 and installs the IZ2S chain into
   the Civ7 trigger save. Follow the existing save-backup precautions before
   installing persistence.
4. If CivSwitch is already at **Awaiting payload**, skip Wi-Fi setup and run:
   ```sh
   ./iz2sciv.sh install
   ```
5. Close and reopen Civ7, then open **Load Game** to execute the installed chain.

## Launch

With the persistent chain installed, open Civ7 → **Load Game**. Filesystem
acquisition and IZ2S startup run in sequence without PC commands.

For a standard CivSwitch loader currently showing **Awaiting payload**, run:

```sh
./iz2sciv.sh launch
```

The IP is discovered automatically using the existing RUN25 loader protocol
check. An open FTP endpoint alone is not treated as a CivSwitch loader.
In IZ2S Platform, open **Console IP: automatic search or address selection** on steps 3–5.
Use **Find console addresses**, choose a suggested IP and click **Check and use
selected address**. Loader replies are distinguished from unverified network
candidates. If the list is empty, enter the IP displayed on the console.
The selected address is checked again before installation or one-time launch;
**Use automatic detection** returns to automatic mode. These checks require
`Awaiting payload` on the console. CLI users can select `--host IPv4`.

Wait for `IZ2S CORE IS RUNNING`, close Civilization VII normally, and hold **R**
while opening a Switch 2 game. Repeat the R-held launch once if the first run
only records that game's policy. IZ2S opens `/iz2s/izlauncher.iza`, using the
same resources, applications and PC-link configuration as the SB4 route.
Restart the console before another native launch attempt on the same boot.

## Local checks and compatibility

```sh
./iz2sciv.sh build
./iz2sciv.sh check
node --test tools/iz2s.test.mjs
```

`build` produces `dist/core.js` and `dist/persist/*` without contacting a console.
`npm run send`, `send:chain`, `persist:chain` and `persist:split` all target IZ2S.
The old `install-kaihen.sh`, `tools/kaihen.mjs` and `src/kaihen.js` names remain
as compatibility aliases to IZ2S; they cannot select a kaiHEN release.
`npm run send:fs -- IPv4` is the explicit filesystem-only research entry.

Native transport helpers remain in `src/kaihen/` to retain upstream provenance;
that directory name does not select the runtime. The bootstrap ABI is the same
432-byte layout used by RUN25's SB4 `start-core.cjs`; Civ7 keeps its own thread adapter.

Local build/ABI tests and FTP file verification do not prove a successful
Civ7 native boot. That requires a fresh console boot and an on-device launch.
Already-installed Civ7 saves contain their old payload until `install` updates it.
RUN25 labels installations without the new save name as needing an update.
The installed and LDN boot title is `IZ2s 0.3.1 by IZMYSH`; upstream notices
and the single-line acknowledgments are retained.

## License

CivSwitchFS is MIT. Code ported from ns2jb and kaiHEN retains GPL v2.0 terms.
See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Acknowledgments

Thanks to Nerisma (CivSwitchFS), ave (the kaiHEN port), Gezine (CivSwitch), Slapah/Hokage (the CivSwitch filesystem port), pepsipu (ns2jb), Kurumuz (kaiHEN and the ns2jb fork), Alula (stability improvements), the civswitch-kit and civ7-fs contributors, epiccsolar (independent kaiHEN work), and IZMYSH (RUN25 integration). [CivSwitchFS](https://github.com/ItsNerisma/CivSwitchFS), [CivSwitch](https://github.com/Gezine/CivSwitch), [Slapah/Hokage attribution](https://github.com/byizmysh/IZCIVSwitch/blob/main/NOTICE.md#upstream), [ns2jb](https://gitlab.com/pepsipu/ns2jb), [Kurumuz’s ns2jb fork](https://gitlab.com/kurumuz/ns2jb), [civswitch-kit](https://gitee.com/mza1010/civswitch-kit), [civ7-fs](https://github.com/HerXayah/civ7-fs).

LLMs were involved in the development of this. Please don't use it if you don't trust it.

