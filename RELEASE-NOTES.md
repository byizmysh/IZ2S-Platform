# IZ2S Platform 0.21.0

These notes cover the cumulative changes **since the published [0.16.0-rc.5](https://github.com/byizmysh/IZ2S-Platform/releases/tag/v0.16.0-rc.5)**, including offline launcher use, console sleep/wake, Cardputer ADV input, integrated module tabs, improved file browsing and new desktop installers.

## Offline use and console sleep

**IZlauncher and IZPSlauncher can keep the loaded console menu and supported local content running offline after the initial PC launch.** Losing the PC link no longer automatically shuts down the active console menu. Installed local content does not require internet access.

**Switch 2 can be put to sleep with the launcher open and resumed after waking**, provided SmileBASIC and its native session remain running. The menus preserve their state across pauses and clear stale input on resume; repeating First Setup is not required.

PC-managed module launches and returns, FS access and streaming still require an active PC connection. Online applications also need internet access. A disconnected PC link is not automatically reconnected; starting a new native session after ending the retained one still requires a full console restart. See the [offline and sleep FAQ](https://github.com/byizmysh/IZ2S-Platform/blob/main/FAQ.md#offline-use-and-console-sleep).

See the [panel and console-menu screenshots](https://github.com/byizmysh/IZ2S-Platform#screenshots).

## Upgrade notes - read first

The default engine/bootstrap has changed. Version 0.16 used `NS2I`, port **39395**, nine START arguments and BASIC SHA-256 `2261f65fb9ec0ed708ce0f0b6da5057168f706e541d956f658a3c2655e9e8c36`. Version 0.21's IZ2S Core uses **NS2IZM**, port **39380**, five arguments and SHA-256 `5a2b86da8787318652365b2e4cf710f67ce618d6c21087dfc78716533a510d32`.

**Run First Setup again when upgrading from 0.16. Renaming the old BASIC file is insufficient.** Afterwards fully restart Switch 2, open fresh Direct and use Launch. A currently compatible `NS2IZM` from a 0.21 build is a different case and does not need replacing merely for installer/documentation changes.

Install into a new versioned directory. Re-select device/network options; preserve private pairing/SSH material and module data deliberately. Do not copy old live-session state, engine snapshots or the entire old runtime tree over the new installation. Existing installations and console programs are not deleted automatically. See the [migration FAQ](https://github.com/byizmysh/IZ2S-Platform/blob/main/FAQ.md#updating-from-016).

## PC installation and host portability

- Add **Install.cmd / Start.cmd** and the PowerShell bootstrap for Windows x64; add **Install.command / Start.command** and a shell bootstrap for Intel/Apple silicon macOS.
- Windows/macOS download checked private Node.js 24 and uv, prepare private Python 3.12/Pillow 12.0.0 and create a per-user shortcut. Windows supplies a checked portable OpenSSH client when no client is available; its pinned fallback is an upstream Preview build. No SSH server is installed.
- Add the Linux desktop-file entry and optional automatic panel opening after installation. Linux prepares packages through apt/pacman/dnf; manual prerequisite control remains available. Existing directories are never overwritten.
- Add a common managed-Python resolver for host rasterization/dependency checks, including paths with spaces and Windows's interpreter naming. Start entries request background server startup and a browser window.
- Replace mandatory Linux `ip`/`ping` route probes with Node.js network APIs. Distinguish an available route from an actual console link. Linux SD identity discovery no longer calls `findmnt`.
- Bundle licensed DejaVu fonts rather than requiring distribution-specific font directories. Separate runtime dependencies from developer shader tools. No npm install, Homebrew or Xcode is needed for the prepared runtime.
- **Windows/macOS installation/panel targets are experimental and need actual OS tests. Native console session declarations remain Linux-only.** Add explicit host/component compatibility tables and an installation-to-shutdown test plan; do not equate portable networking or browser display with a qualified native host.

## Naming and connection engines

- Use **IZ2S Platform** for the panel, **IZlauncher / IZPSlauncher** for console menu styles, and **IZ2S Core** for the default engine. IZMYSH is the developer credit. Remove visible RUN labels from branding, shortcuts and archive names.
- Rename the former `ns2jb2` engine directory to **BermTheSequel**, update its paths and retain its own saved program **NS2B**. Both current bundled engines use the same TCP port and compatible console protocol.
- Maintain IZ2S Core in its own `engines/izmysh/` tree with its own host-contract, transport record reader and FS pipeline lifecycle implementation. Its native ancestry/ABI and upstream notices remain explicit; the name does not claim new authorship of the underlying research.
- Package both current engine runtime inventories completely; restore dependencies previously missing during the earlier copy, including launcher input-check/runtime files. Validate resource and bootstrap hashes before launch.
- Keep the original **ns2jb by kurumoz** adapter (`kurumuz`, `NS2K`) as an explicit separate source installation. The older pepsipu adapter is not part of the current public profile. Do not describe these engines as interchangeable saved BASIC files.

## Console launchers and session lifecycle

- Ship both menus at **0.21**, with bundled-font rendering and clearer progress descriptions.
- Retain the selected-shell/module-format compatibility behavior fixed in 0.16.0-rc.5; IZPSlauncher continues to accept the IZlauncher module format while enforcing platform/ABI requirements.
- Simplify Stop/return-to-Launch handling and prevent misleading preparation/waiting states after a live console connection exists. Report stage failures from startup and input initialization rather than dropping them before diagnostics start.
- Use fresh local HTTP RPC connections in both bundled engines to avoid stale pooled sockets causing `socket hang up` / `ECONNRESET` during launcher input initialization. Do not replay a request whose outcome is unknown.
- Optional input initialization no longer hides transport loss and continues with additional native operations. FS acquisition that retains threads/handles/cache ownership remains a failed attempt with an explicit console-restart requirement; automatic retry is disabled.

## ARM PC and Cardputer ADV input

- Bring the complete [ARM input installer/package](https://github.com/byizmysh/IZ2S-Platform/blob/main/hardware/agents/arm-input/README.md) into this distribution.
- Add the [Cardputer ADV](https://github.com/byizmysh/IZ2S-Platform/blob/main/hardware/devices/cardputer-adv/README.txt) hardware profile, Wi-Fi command transport and separate First Setup sequence. The ARM board's existing setup flow remains separate.
- Supply an M5Launcher application image, a manual File/ZIP workflow and Linux positively identified USB-storage copying. Update only the IZ2S app/configuration package; preserve the launcher, other firmware and existing Wi-Fi settings.
- Preload the full text/actions to ADV SD. Later server requests select local keyboard actions instead of streaming the source character by character. Add cancellation, lost-contact handling and no automatic replay of accepted jobs.
- Add an on-device header/footer menu, Wi-Fi selection/manual SSID, keyboard-entered password and Tab show/hide. Wi-Fi setup appears on first use/disconnection. Explicitly ask the user to move ADV USB from the PC to Switch 2 after Wi-Fi setup.
- Accept plain IPv4 entries and normalize the HTTP endpoint. Explain the local `iz2s-adv.local` mDNS hostname and the displayed-IP fallback.
- Preserve intended command/source capitalization with the selected SmileBASIC Caps Lock state. Firmware **0.21.1** uses unnumbered eight-byte boot keyboard reports, verifies report completion and exposes send errors instead of reporting false success.

- Update ADV to firmware **0.21.3**. Report USB host/configuration/suspend state over Wi-Fi, show keyboard readiness automatically and remove the redundant manual USB confirmation. Keep the console-screen confirmation separate from USB readiness.
- Add idle USB reannouncement after a console restart when the host has not configured the keyboard. Wait 8 seconds before the first 400 ms detach/reconnect, back off further attempts, leave active input/configured sleep alone, and never replay accepted commands. USB status does not claim to identify Switch from its host model.
- Recognize a matching ADV USB serial attached to this Linux PC and block console keyboard jobs there. Clear stale screen confirmations when the device or USB connection changes, and explain nonresponsive USB hosts without assuming the cable is absent. See the [ADV recovery FAQ](https://github.com/byizmysh/IZ2S-Platform/blob/main/FAQ.md#why-does-adv-sometimes-need-usb-reconnecting-after-switch-restarts).

## Modules, workspace and files

- Synchronize module visibility rules between the server and the panel. Keep diagnostic packages hidden while allowing the separate DOOM Prime module in the library.
- Fix initial panel loading with an empty App library, including Core only mode. Rendering errors now show a view error instead of incorrectly reporting that the local server is unavailable.
- Open module interfaces/settings as IZ2S workspace tabs, including separate modules using the SDK. Add consistent active selection, quick reopen, sidebar status and accessible Stop controls. Closing a view is distinct from stopping a service or the console session.
- Remove stale Tools and their iframes after a panel restart or the end of their console session. Check actual listener availability instead of trusting a persisted Running label, preserve live prelaunch settings, and ignore late Open/Stop responses for removed tools. Retained native resources remain an explicit recovery condition.
- Add an explicit Close view control and consistent spacing between App library actions. Both console menus accept the same ABI-1 module shell contracts.
- Close other settings windows even if one reports an unconfirmed shutdown, while retaining that failure and preventing automatic cleanup replay.
- Reject incomplete/changed release sources before installation, restore executable entry permissions from the source manifest, and prevent installing recursively inside the extracted source tree. Bind dependency checks to the new installation instead of an inherited platform path.
- Respect Windows mixed-case Path variables during browser discovery and retain the selected macOS browser, including a custom default.
- Open retained FS access as the [Super File Bros.](https://github.com/byizmysh/super-file-bros) browser page first, with external file-manager launch as a later choice. Its standalone GitHub package remains independent of IZ2S.
- Improve file navigation with Back/Forward/Up, breadcrumbs and a compact responsive toolbar. Keep installed-game exports, general SD storage and SmileBASIC title-owned storage distinct. Internal filesystems and FTP exports stay read-only; supported browser SD writes are explicit.
- Include [File Exchange](https://github.com/byizmysh/IZ2S-Platform/tree/main/modules/iz-exchange), [File Exchange FS](https://github.com/byizmysh/IZ2S-Platform/tree/main/modules/iz-exchange-fs) and [Book Reader](https://github.com/byizmysh/IZ2S-Platform/tree/main/modules/BookReader) with rebuilt console assets, dependency declarations and private files excluded. File Exchange FS depends on File Exchange. Book Reader includes TXT/unencrypted EPUB import, typography/library settings and local reading progress; only the welcome content is distributed.
- Add **SD:/IZ2S/Homebrew/Modules/<module.id>/** as the common interaction directory. Preparing writable SD storage creates installed-module folders. Lifecycle/settings/session/runtime contexts expose `sdStorage`; the SDK's explicit creation helper preserves contents and reports failures without replay. Modules must adopt this path themselves; no automatic migration of their old files, ROMs, BIOS or saves is performed.
- [BlitBun](https://github.com/byizmysh/BlitBun), [VacooTube](https://github.com/byizmysh/VacooTube), [emulators and other modules](https://github.com/byizmysh/IZ2S-Platform/blob/main/modules/README.md#additional-modules) remain separate packages with their own OS/native-runtime requirements.

## Documentation and release packaging

- Produce a clean source ZIP, Linux tar.gz and explicitly **experimental** Windows/macOS ZIPs with one complete installation tree, per-file SHA-256 inventory and archive checksums. The platform labels identify installation entry points, not completed native-host qualification.
- Rebuild launchers, the three bundled modules and the independent ARM package in an isolated staging tree. Keep firmware source/image integrity, fonts/licenses, engine inventories and module dependencies checked.
- Exclude private profiles/keys, imported books, extra module runtimes, logs, test data and live engine snapshots. No build/installer action uploads or publishes files. A manual future Windows/macOS installation workflow can be prepared without dispatching it.

Windows/macOS installers still require testing on their respective systems. Native console sessions currently remain Linux-only. See the [compatibility table](https://github.com/byizmysh/IZ2S-Platform/blob/main/COMPATIBILITY.md) for current platform requirements.
