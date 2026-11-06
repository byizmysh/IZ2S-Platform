# IZ2S Platform - FAQ

This guide describes **IZ2S Platform 0.21**, compared with the published [0.16.0-rc.5](https://github.com/byizmysh/IZ2S-Platform/releases/tag/v0.16.0-rc.5). Read [README.md](README.md) for installation and setup, and [COMPATIBILITY.md](COMPATIBILITY.md) before choosing a host OS.

## Recommended console and network setup

### What should I prepare before installing IZ2S?

For FS access, start in **handheld mode** and use **wired LAN** between the console and the local network shared with the PC. These are recommended starting conditions, not a guarantee of successful FS acquisition. Keep the ARM PC or ADV USB keyboard connection available; follow the input device's connection guide for your physical setup.

### How do I keep local access while restricting internet access?

- **[SWFence](https://github.com/byizmysh/SWFence):** configure its OpenWrt router firewall for the console, permit the PC's local IPv4 subnet and leave internet exceptions empty for LAN-only use. Register each console Wi-Fi/Ethernet adapter MAC you use. Apply and check the rules before connecting the console; follow the project's [installation guide](https://github.com/byizmysh/SWFence/blob/main/INSTALL.md).
- **[90DNS](https://gitlab.com/a/90dns):** follow its instructions for each console network connection. It filters Nintendo-related DNS names; it is not a firewall and does not block all internet destinations or direct-IP traffic. The project describes its Switch 2 support as early, so verify the result on your network.

Keep the PC, console and input device reachable on the local network. With restrictive router rules, use the PC/ADV local IPv4 addresses if discovery or `.local` names do not resolve. The PC can retain its own internet connection for installation, downloads and online modules; the console does not need unrestricted internet access for the local IZ2S connection.

## Installation and OS prompts

### Which file do I open?

Extract the whole archive to a writable local folder first. On Windows double-click **Install.cmd**. On macOS double-click **Install.command**. On Linux open **Install-Linux.desktop** and allow launching if your desktop asks; if unsupported, open a terminal in the folder and run `sh install.sh --launch`. Do not run an installer directly inside a ZIP, move a single script away from its files, or use a network share as the installation folder.

Prepared releases include `SOURCE-MANIFEST.json`. Both installer paths check its required source files and hashes before copying; a missing or changed file stops installation. Extract a fresh complete archive if this check fails. The manifest also restores executable permissions during installation when an extractor loses those bits. Keep the new installation outside the extracted source folder.

The installer creates a new per-user installation, prepares dependencies, checks its files, adds a shortcut and requests browser startup. It does not type on a console, install a board OS, flash firmware, create an SSH server or upload files anywhere. Runtime downloads are inbound from official project hosts/PyPI.

### Do I need to install Node, Python, npm or libraries manually?

Windows/macOS bootstrap private Node.js 24, then use a checksummed uv executable to prepare private Python 3.12 and Pillow 12.0.0. Their runtime files live inside the installed copy's `data/runtime/`. Pillow is installed from a binary wheel; no compiler is requested. No global npm packages, Homebrew or Xcode are needed. Windows uses a working OpenSSH client when present; otherwise it downloads the checksummed portable Microsoft OpenSSH package and uses its client only. The pinned portable fallback is currently an upstream Preview build; it also requires Windows testing.

Linux uses apt, pacman or dnf for Python/Pillow, SSH and download/archive tools. It reuses a compatible Node or downloads private Node 24. Fonts are bundled. `ip`, `ping`, `findmnt`, shader compilers and system DejaVu packages are not base runtime requirements. On another distribution, provide Node 20.20+, Python with Pillow and SSH, then use `sh install.sh --no-system-packages`. Developer builds additionally need shader compiler/validator tools.

The first installation requires internet access. The base panel can use its installed runtimes afterwards; optional engine/module downloads and online applications still need their own network access.

### What permissions can Windows request?

The CMD entry runs its bundled PowerShell script with a **process-only** execution-policy option; it does not change the machine's permanent execution policy. Downloaded-file/SmartScreen controls, antivirus, AppLocker or organization policy may still block it. Review the package's origin and the exact warning. For a trusted ZIP, **Properties → Unblock → Apply**, if offered, can be used before extracting it again. Do not disable antivirus or organization policies. On managed computers, an administrator may need to approve the package. See [Microsoft's execution-policy explanation](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_execution_policies).

No administrator rights are expected for the private runtimes or Start-menu shortcut. Use a normal account and writable user profile. The Windows Store Python alias is not used once private Python is installed. On first network use, Windows may request firewall permission; approve only the intended executable and local/private network. The installer does not silently add firewall exceptions.

### What permissions can macOS request?

macOS can block a downloaded unsigned script/app or request local-network access. Review the file's origin; when macOS offers it, use **System Settings → Privacy & Security → Open Anyway** for that specific file, then confirm. Do not disable Gatekeeper globally or remove quarantine from unrelated files. Apple's [instructions for downloaded apps](https://support.apple.com/en-us/102445) explain the available approval flow.

If extraction removed execute permission, open Terminal in the extracted folder and run:

```sh
chmod +x Install.command Start.command
./Install.command
```

The Applications shortcut is a local unsigned wrapper in `~/Applications/`; it is not an App Store or notarized installer. It opens the installed Start file through Terminal, which requests background server startup and a browser window. macOS may ask for local-network/file access when a feature first needs it. The installer does not install Homebrew, Xcode, Rosetta or global Python. Native ARM64 runtime selection is used on Apple silicon; Intel Macs use x64.

### Why is there an extra step on some Linux desktops?

Executing a downloaded `.desktop` file is controlled by the desktop/file manager. GNOME/KDE and other desktops can require **Allow Launching**, an executable permission, or running an installer from Terminal. This is an OS policy, not a dependency failure. `sh install.sh --launch` is the universal Linux entry point. Package installation may request sudo. On Arch the installer uses a synchronized package-manager upgrade; review its prompt rather than interrupting a package transaction.

### Can I use Windows/macOS for the complete console workflow now?

The installers and PC entry files are prepared for **testing**. They have not been exercised on real Windows/macOS machines. The current engine, shell and hardware session descriptors still declare Linux only, so native console sessions are not enabled on Windows/macOS in this candidate. Installer success or a visible browser must not be reported as an end-to-end console pass.

Before enabling those hosts, audit and test graceful Stop, subprocess ownership, retained filesystem IPC, card discovery, firewall/discovery behavior and each module's host programs. The [compatibility matrix](COMPATIBILITY.md) separates implementation from qualification; [HOST-TESTING.md](distribution/HOST-TESTING.md) gives the checklist. Linux ARM64 also needs its full physical configuration tested.

### Where is IZ2S installed, and how do I open it again?

The exact folder is printed by the installer:

| OS | Default location | Reopen |
| --- | --- | --- |
| Linux | `~/.local/share/iz2s-platform/releases/<version>-<timestamp>/` | Application shortcut or installed `start.sh` |
| Windows | `%LOCALAPPDATA%\IZ2S Platform\releases\<version>-<timestamp>\` | Start-menu IZ2S Platform link or installed `Start.cmd` |
| macOS | `~/Library/Application Support/IZ2S Platform/releases/<version>-<timestamp>/` | `~/Applications/IZ2S Platform.app` or installed `Start.command` |

A compatible running panel is reused by its entry file. The browser is an interface; the server runs on the computer that launched it. Closing the browser does not necessarily stop that server or its console session. Do not delete an installed folder while its processes are running. For foreground diagnostics use the private Node executable with `start.cjs --preview --no-open`; see the testing guide.

### Can I choose a destination or install without opening anything?

The destination must be new; existing installations are never merged or overwritten.

```text
Windows: Install.cmd -Destination "C:\Users\me\IZ2S trial" -NoShortcut -NoLaunch
macOS:   sh distribution/install-macos.sh --destination "$HOME/IZ2S trial" --no-shortcut --no-launch
Linux:   sh install.sh --prefix "$HOME/IZ2S trial" --no-desktop --no-system-packages
```

The Linux `--no-system-packages` option requires prerequisites to be present; private Node can still be downloaded. For unattended Windows testing, call `distribution/install-windows.ps1` directly from PowerShell rather than the CMD wrapper, which pauses to keep diagnostics visible.

### A download or package check failed. What next?

Keep the printed error. Check connectivity to `nodejs.org`, `github.com`/GitHub asset hosts and `pypi.org`/`files.pythonhosted.org`. A proxy or antivirus can block a legitimate download. The installer stops on a failed checksum and does not execute that archive. It never changes certificate validation to ignore TLS errors. uv can use the system certificate store through its documented `UV_SYSTEM_CERTS=1` option if your managed network requires it; coordinate certificate/proxy changes with its administrator.

Existing installations are preserved. A failed new installation folder is retained for diagnosis and is not considered ready. Remove only that incomplete folder before retrying its exact destination, or choose another destination. An incomplete Node bootstrap is under the OS-specific `IZ2S Platform/bootstrap/node-<arch>/` directory; remove that specific incomplete bootstrap if the installer explicitly reports it. Do not delete the old release or private settings as a generic repair.

### No browser window opened. Is the installation broken?

Open the installed Start file again. Check `data/state/startup.log` in the installed folder; it is private and may contain the panel access URL. Confirm a browser is installed. Browser launch uses the OS adapter; different default-browser versions may open a tab instead of a separate window. `IZ2S_BROWSER` can point to an installed browser executable. A browser on another OS does not move the PC server to that OS. The default authenticated panel listener is local to its host; remote access is not automatically enabled.

## Offline use and console sleep

### What works offline?

After the initial PC launch, both console menus can keep their already loaded interface and supported local content running without the PC link. Installed local content does not require internet access. This does not make a PC-backed module independent of its server: module loading/switching, returns from host-managed applications, FS RPC and streaming need an active PC connection. VacooTube and other online services also need internet access.

If the link is lost while the console renderer is active and no module transition is being serviced, IZ2S retains the native resources instead of shutting the menu down automatically. The panel reports the session as offline; it cannot verify console execution through a broken connection. A link lost during native setup or a module transition can still fail the session. The host does not reconnect automatically or replay native commands.

### Can I put the console to sleep?

Yes. Leave the launcher open, put Switch 2 to sleep and wake it to continue, provided SmileBASIC and the native session remain running. The menus retain their state across pauses and discard held-button/touch input on resume. Sleep does not require repeating First Setup.

Keep the PC server running when using PC-backed features. If sleep breaks the PC link, the same offline limitations apply; waking the console does not guarantee reconnection. Closing SmileBASIC, ending the native session or a native failure is different from sleep. For a new native launch after an ended or failed session, fully restart Switch 2, open fresh Direct and use Launch.

## First Setup, input devices and networking

### What are the names?

IZ2S Platform is the PC panel, IZlauncher/IZPSlauncher are console menus, IZ2S Core is the default connection engine, and IZMYSH is its developer credit. `izmysh` stays the internal engine ID for compatibility. The bundled alternative is **ns2jb by BermTheSequel**. The original **ns2jb by kurumoz** has an optional adapter with internal ID `kurumuz`; install its source explicitly. The old pepsipu adapter is not part of the 0.21 public profile.

### What does First Setup do, and when do I restart?

It sends the engine's BASIC source through the chosen keyboard device and saves it in SmileBASIC. It is separate from installing the PC app. Then fully restart Switch 2, open fresh SmileBASIC Direct and use Launch. The current configured target is system 23.0.1 and SmileBASIC 4.4.9. Changing firmware/SmileBASIC versions requires qualification. A failed or ended native session requires another full console restart; returning to Direct alone is insufficient after retained native resources.

### Does the ARM board need reinstalling after an IZ2S update?

Compatible **input API 1** agents are checked against the board's own installed inventory, independent of the launcher's version. Do not reinstall a healthy compatible agent merely because the PC version changed. The board itself still needs Linux, Python, SSH authentication and a working USB device/OTG keyboard gadget. Its package does not supply an OS image or modify arbitrary USB drivers. Use the [ARM guide](hardware/agents/arm-input/README.md).

### Why does ADV sometimes need USB reconnecting after Switch restarts?

The dock may not configure the keyboard again after a console restart. ADV firmware **0.21.3** can request a new USB connection while idle and while the authenticated panel is checking status: it waits 8 seconds, detaches for 400 ms, then reconnects. Further attempts back off to 16, 32 and 60 seconds. It does not reset an active input job or a configured sleeping USB host, and it never replays commands.

If the panel reports that the USB host is not responding, check the dock connection, USB data cable and awake console. USB power or Wi-Fi connectivity alone does not prove that the keyboard is configured. Software reconnect cannot repair an unavailable dock port. The USB device cannot uniquely identify the host as Switch; on Linux the panel can separately recognize the same ADV connected to this PC and block console input there.

Update through the [ADV firmware procedure](hardware/devices/cardputer-adv/README.txt), preserving M5Launcher, the other firmware and the existing pairing/Wi-Fi settings. ARM input does not need an update for this change.

### How do I set up ADV without replacing my other firmware?

In First Setup choose Cardputer ADV. Connect it to the PC, select **USB storage** in M5Launcher and use the Linux direct-copy option when the card is positively identified. On Windows/macOS, or if automatic detection is unavailable, choose **File**, extract its ZIP to the root of the ADV card and eject it normally. The package adds `Utilities/IZ2S-ADV-0.21.bin` and its `IZ2S/input.json`; it does not replace M5Launcher or remove other firmware. Do not write the application image at flash address zero. Existing Wi-Fi configuration is preserved by the supported copy workflow.

Exit USB storage, start the IZ2S application in M5Launcher, select the local Wi-Fi shared with the PC and enter its password on the ADV keyboard. The firmware shows keyboard controls in its footer; Tab shows/hides the password. Wi-Fi selection is offered on first use or when disconnected. Now move the USB **data** cable to Switch 2. The panel detects USB readiness automatically over Wi-Fi; no manual cable checkbox or console LAN session is needed for this check. The PC continues to communicate with ADV over Wi-Fi.

### What is iz2s-adv.local? Can I enter a plain IP?

It is the firmware's local mDNS hostname, not an internet service. Resolution depends on the network and host OS. The address on the ADV screen, such as `192.168.1.50`, can be entered directly; IZ2S adds `http://`. Both devices must be reachable on the same local network. Guest/client-isolated Wi-Fi or a firewall can block communication even when ADV shows Wi-Fi connected.

### Why is text lowercase or punctuation wrong?

Use the expected SmileBASIC English/US keyboard layout and match **Switch keyboard Caps Lock state** in Device settings to the console. ADV compensates for that setting while preserving the intended case of source strings. Do not convert entire BASIC source text blindly to uppercase, since quoted data can be case-sensitive. A Wi-Fi connection alone does not prove ADV is attached as a keyboard: its USB data cable must be on Switch, and firmware report errors must remain zero.

### Does “route available” mean Switch is connected?

No. Node.js can select a route/interface without contacting the console. The native handshake establishes the real console link. Prelaunch pending/automatic-address states are distinct from that live link. The common PC network code does not require Linux `ip` or `ping`. UDP discovery and mDNS can still be filtered by the host firewall, hotspot settings or access point.

## Modules and file access

### Does closing a module tab stop it?

It closes the view immediately, just like **Close view** in its toolbar. Use the sidebar's **Stop** or the tab's **Stop module** to stop its services; use the sidebar to reopen the view. This also applies to separately installed modules that use the workspace interface. Stopping a module is separate from ending the console session. An uncertain native stop is reported as retained rather than pretending resources were released.

### Why did an old Tool disappear after reconnecting?

Tools belong to the panel instance and, when opened for a console session, that session. Ended-session Tools and dead local pages are removed from the sidebar and tabs instead of remaining marked Running. Live settings opened before Launch can stay available without a console. Closing a view does not prove that native resources were released; an unconfirmed native stop remains visible as a recovery warning.

### Do modules depend on the console menu's appearance?

IZLauncher and IZPSlauncher share the ABI-1 module interface and accept each other's module shell declarations. Loading, removing, settings and Stop use the same platform services. A module can still require a particular capability, host platform or console ABI. PS2 file settings require an active session with verified FS access; changing the menu skin cannot provide missing FS access or release resources owned by a running game.

### What is bundled?

The panel, both launchers, [IZ2S Core](engines/izmysh/README.md), the BermTheSequel alternative, [ARM](hardware/agents/arm-input/README.md)/[ADV](hardware/devices/cardputer-adv/README.txt) input packages, [Super File Bros.](https://github.com/byizmysh/super-file-bros) integration, [File Exchange](modules/iz-exchange/README.md), [File Exchange FS](modules/iz-exchange-fs/README.md) and [Book Reader](modules/BookReader/README.md). [BlitBun](https://github.com/byizmysh/BlitBun), [VacooTube](https://github.com/byizmysh/VacooTube), [emulators and other modules](modules/README.md#additional-modules) are separate installations. Their own host-platform declarations, native executables and dependencies still apply.

### Where should modules store their SD files?

Use `SD:/IZ2S/Homebrew/Modules/<module.id>/`, where `module.id` is the stable lowercase package ID. For example, `ps2-emulator` uses its own `ps2-emulator/` directory. It survives display-name/version changes while the ID stays stable. Opening SD storage prepares folders for installed modules. Modules can use `context.sdStorage` and its `ensure(storage)` helper. Existing files are not moved, and installed modules are not automatically rewritten to this convention. See [SDK SD storage](sdk/SD-STORAGE.md).

### What does FS endpoint do?

Enable it before Launch to acquire and retain the separate FS RPC capability. Super File Bros. opens as a workspace page instead of immediately handing off to an external file manager. It can browse installed-game exports and the filesystems actually opened in that session. This is separate from File Exchange's SmileBASIC-title `/save`, `/tmp` and read-only `/rom` folders. Internal storage and FTP exports are read-only; SD changes are explicit browser operations. The UI never invents unavailable mounts.

### Can Explorer or Finder open the console files?

An external manager accesses the exported **FTP** service, not the raw FS RPC interface or a normal local disk. Windows Explorer's FTP behavior depends on the OS configuration and is not a general writable mount. Apple documents read-only FTP access in Finder's **Go → Connect to Server** workflow; client/OS behavior must still be tested ([Apple server types](https://support.apple.com/guide/mac-help/servers-and-shared-computers-you-can-connect-to-mchlp3015/mac)). A dedicated FTP client can be selected where supported. The server exports read-only FTP regardless of the client's write UI. Use the integrated browser for supported SD uploads/changes. SMB/WebDAV/FUSE integration is not supplied.

A file manager is launched on the PC running IZ2S. From another computer, use the displayed FTP address with a compatible client and an allowed route. Do not assume Explorer/Finder features or credentials are automatically provisioned. FTP access exposes the selected export while it is running; disconnect it when finished.

### How do I go back, and what does Disconnect export stop?

Use Back/Forward for navigation history, **Up** for the parent folder, or the breadcrumbs for a specific ancestor. Folder actions share a compact toolbar that wraps on narrow windows. **Disconnect export** closes that file export; it does not end the engine or console session. Likewise, closing the Super File Bros. tab leaves the module/service running until Stop.

### Are books and emulator files included in public builds?

Only Book Reader's built-in welcome content is included. Imported books, reading positions, settings, ROMs, BIOS images, caches and downloaded native runtimes stay private. Modules choose their own supported file types and host dependencies. The standalone Super File Bros. package remains independent of IZ2S; this repository's integrated edition does not change that distribution contract.

## Updating from 0.16

### Can I reuse the old saved BASIC program?

The published 0.16.0-rc.5 default engine used **NS2I**, port **39395**, a different bootstrap checksum and **nine** START arguments. Current IZ2S Core uses **NS2IZM**, port **39380** and **five** arguments, sharing the current console protocol with the bundled BermTheSequel alternative. Run First Setup for the new core. Renaming the old `NS2I` program is insufficient. This is different from upgrading within compatible 0.21 builds, whose already-current `NS2IZM` may remain usable.

The bundled alternative uses `NS2B`; the optional kurumoz adapter uses `NS2K`. Do not overwrite the wrong BASIC program based only on a similar engine label. Old saved programs are not deleted by the PC installer.

### What settings and modules should I carry over?

Keep the old installation as a backup; install 0.21 into its new folder. Before opening the new copy, gracefully stop the old console session/modules and its PC server: two copies cannot own the same panel/listener ports. The installer does not silently terminate the old session. Re-select the engine/input/network settings in the new panel. Deliberately preserve required pairing and SSH credentials; those are private and never placed in release archives. Do not copy the entire old `data/`, runtime snapshots, `bootstrap-chain/`, native connection state or engine directories into the new release. Those can contain incompatible process ownership and old ports/protocols.

Reinstall compatible module packages, preserving their own documented data directories. Check every optional module's target OS/native programs. Match the new shortcut to the intended version. A compatible ARM input API 1 agent can stay on its board. Existing ADV credentials should be preserved when updating the app package. See the full [changes since 0.16](RELEASE-NOTES.md).

## Errors, recovery and testing

### Is a retained-memory/native FS failure an ordinary successful setup?

It is a failed attempt, even if an earlier connection step passed. If acquisition reports retained resources, changed heap/cache ownership, a native batch that did not terminate or incomplete release proof, stop and fully restart the console before retrying. IZ2S deliberately does not replay uncertain native writes. Preserve local diagnostics if investigating, but sanitize keys, private addresses, listings and file names before sharing them.

### What if FS acquisition keeps failing?

Try this recovery sequence if repeated attempts stall or fail while acquiring FS memory:

1. Stop the IZ2S session and **fully restart Switch 2**. Do not retry native acquisition in the failed session.
2. Return to **handheld mode** and check the **wired LAN** connection to the PC's local network.
3. Open any installed game, leave it running for about **10 seconds**, then **close the game completely** from HOME.
4. Only then open **SmileBASIC 4 → Direct**, confirm the console screen in IZ2S and use **Launch** with **FS endpoint (--rpc)** enabled.

This is a suggested workaround for recurring acquisition failures, not a guaranteed repair. If it fails again, stop and follow the reported restart/recovery requirement rather than repeatedly pressing Launch. Running a game does not prove that resources from a failed native attempt were released; the full restart comes first. Repeating First Setup is unnecessary unless the saved BASIC program itself needs updating.

### What changed for socket hang up during launcher initialization?

Both bundled engines now use fresh local HTTP RPC connections rather than pooled sockets. Errors during input/app-runtime initialization are recorded by the launcher diagnostics. Optional input features do not swallow transport loss and continue writing. Lost responses are still not replayed automatically. A physical Linux/ADV/IZPSlauncher run reached the frame loop after this change; this does not qualify every OS or module.

### What still needs a full test?

Use [HOST-TESTING.md](distribution/HOST-TESTING.md) from clean installation through startup, First Setup, launch, modules, Stop and restart. Windows/macOS need their own OS execution and native-host port/qualification before full console testing is enabled. Long sessions, all alternate engines, all module combinations and Linux ARM64 also remain separate tests. Local syntax/unit checks and a visible browser are narrower evidence.

### How do I uninstall?

End the console session, stop modules and close the server first. Keep any needed private settings/books/module files separately. Remove only the installed version folder and its shortcut when no longer needed. Private per-install runtimes are removed with that folder. Bootstrap Node caches live separately under the per-user `IZ2S Platform/bootstrap/` directory on Windows/macOS and can be removed when no remaining installer needs them. Shared SD module data is not automatically deleted. The installer never creates a Windows SSH server service or changes global Python/Node.

### Will building or running these scripts publish anything?

No upload, Git push, release publication or deployment is performed. Source/runtime archives are written locally into `dist/github/`. The included manual OS-installation workflow is only a future testing recipe; it has not been dispatched for this candidate. Do not publish until its intended end-to-end checks have been recorded.
