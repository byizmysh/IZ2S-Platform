# Compatibility and qualification - 0.21 local candidate

**An installer target is not a qualified native console host.** This file separates implemented installation paths, local software checks and physical-device evidence. No Windows or macOS machine was available for this candidate's local checks.

| Host | Installer / dependencies | Panel / browser | Console First Setup → Launch → Stop | Status |
| --- | --- | --- | --- | --- |
| Linux x64 | `Install-Linux.desktop` / `install.sh`; existing system-runtime and private-Node installation checks passed | Locally exercised | Physical IZ2S Core + IZPSlauncher + ADV launch reached the frame loop | Main tested host; full regression/long-run coverage still pending |
| Linux ARM64 | `install.sh`, architecture-selected Node | Declared target | Complete physical configuration not qualified | Needs ARM64 end-to-end testing |
| Windows x64 | `Install.cmd`; private Node/Python/Pillow, existing or portable OpenSSH | Implemented; needs actual Windows testing | Current engine/shell/hardware session descriptors remain Linux-only | Experimental installer/panel target; native sessions not enabled |
| macOS Intel | `Install.command`; private x64 Node/Python/Pillow, system SSH | Implemented; needs actual Intel Mac testing | Current session descriptors remain Linux-only | Experimental installer/panel target; native sessions not enabled |
| macOS Apple silicon | Same file; native ARM64 selection, including Rosetta Terminal detection | Needs Apple silicon testing | Same Linux-only session restriction | Experimental; needs installation, panel and native-port work |

Runtime baseline for the new installers: Node.js 24, CPython 3.12 managed by uv, Pillow 12.0.0 binary wheels, bundled DejaVu fonts. Node's own OS minimums apply; see its [supported-platform list](https://github.com/nodejs/node/blob/v24.x/BUILDING.md#platform-list). Windows ARM64 is outside the requested scope; no Windows ARM64 or 32-bit package is supplied. Linux distributions outside apt/pacman/dnf can provide the listed runtimes and use `--no-system-packages`.

## What needs Windows/macOS testing

| Area | Implemented / remaining work |
| --- | --- |
| Fresh installation | Architecture detection, checksummed bootstrap, private runtimes, clean source copy and package checks implemented; run on a clean account with no Node/Python installed |
| OS prompts and shortcuts | Windows downloaded-file policy/Start-menu link and macOS Gatekeeper/Applications shortcut need real OS testing |
| Paths and permissions | Test spaces, non-ASCII user names, read-only extraction folders, duplicate destinations, cancellation and failed downloads |
| Browser window | OS adapters exist; verify installed Chrome/Edge/Firefox/Safari and reuse of an already-running panel |
| Python rendering | Common interpreter resolver added; test font rasterization and runtime-generated launcher assets with private Python |
| LAN and Wi-Fi | Node-based networking is portable; actual firewall permissions, adapter names, multicast discovery, IPv4 selection and hostname fallback need testing |
| ADV firmware copy | Automatic card identity detection currently implemented only on Linux; Windows/macOS use the manual File/ZIP workflow |
| Native console session | Not enabled by the current host descriptors. Audit/port process ownership, graceful Stop/restart, retained FS IPC and cleanup; qualify using a fresh physical console before enabling each OS |
| FS browser / external manager | Test the integrated browser, download/upload, stop/reopen and native filesystem export only after the host's native session is qualified; Explorer/Finder FTP behavior differs |
| Extra modules | Each module must declare the host and supply its own native programs/libraries. Linux-only module binaries do not become Windows/macOS binaries through installation |

## Device and component evidence

| Component | Current evidence / limit |
| --- | --- |
| Switch 2 + SmileBASIC | Configured target: system 23.0.1, SmileBASIC 4.4.9; other revisions require explicit verification |
| IZ2S Core + IZPSlauncher + ADV, Linux x64 | Physical run completed FS initialization, 43 GPU checks and reached a running frame loop at approximately 60 FPS; this is one path, not every module/engine combination |
| IZlauncher | Included/rebuilt; resource, GPU shader and software checks are separate from complete hardware coverage |
| BermTheSequel alternative | Bundled and protocol/resource checked; all physical combinations not qualified |
| Original kurumoz adapter | Explicit separate source installation; Core only mode; not bundled as a complete original runtime |
| ARM input API 1 | Agent integrity, transport and version-independent compatibility checked; board OS/UDC setup remains device-specific |
| ADV firmware 0.21.3 | Retains the eight-byte boot keyboard and send-error reporting; adds Wi-Fi USB telemetry and idle reannouncement. Local connection/disconnection and a console-reboot check were exercised; other docks and automatic-recovery failure cases still require hardware qualification |
| Module SD directories | Mocked writable-SD checks cover idempotence, concurrency, case handling, blocked files and no replay; this change still needs real SD verification |
| Windows/macOS end-to-end | Not run. Installer syntax/unit checks on Linux cannot substitute for these OS tests |

Use [HOST-TESTING.md](distribution/HOST-TESTING.md) to record an actual installation-to-shutdown run. Until that evidence exists, retain the experimental labels and Linux-only native-session declarations.
