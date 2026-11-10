# IZ2S Platform 0.22.0

Released November 10, 2026. Linux x64/ARM64 packages and corresponding source are available from [v0.22.0](https://github.com/byizmysh/IZ2S-Platform/releases/tag/v0.22.0).

## Changes

- IZlauncher now uses the redesigned tile/library presentation, brand mouse, subtle animated background and consistent button hints. IZPSlauncher remains the alternative menu style.
- Connection controls use common engine/mode heading rows, a consistent PC-to-console path and responsive layout. SmileBASIC Launch explicitly points to Make a Program → Direct.
- Civ7 first setup supports Linux Wi-Fi LDN and USB input; subsequent Launch exposes LAN/Wi-Fi selection. PC readiness is independent of initial LDN prerequisites; console-loader readiness remains separate. Selected launchers start automatically after a verified connection.
- Session and file-manager cards share layout and controls. Files & uploads appears after connection. Super File Bros. is integrated for browsing and uploads, with optional FTP for external clients.
- App-library cards show RAM/SD state, filters and installation controls. RAM modules require explicit choice; the old selection is not restored into a different game/session.
- Attempts includes both results, compact entries, date/result/error filtering and history removal. Finished failures are not replayed as new notifications when the panel is reopened.
- Shared file-service handling and checkpoint-based uploads avoid requiring a second Super File Bros. window. Transfer progress/cancellation are visible; resumed uploads verify the chosen source/checkpoint.
- Bundled apps: File Exchange, File Exchange FS, Book Reader, Super File Bros. and IZPSX emulator 0.5.1. Experimental/test builds and unrelated streaming/emulator packages are excluded.
- Release packaging produces source and Linux archives only. Windows and macOS packaging and entry points are not part of 0.22.

## Civ7 setup and recovery

Civ7 First Setup now exposes Start over and Stop beside its mode selector. Reset preserves PC setup, keys and installed saves, clears current progress/errors and retains attempt history. Waiting for an unopened loader is neutral; actual retained-session disconnects remain visible. The final setup launcher action can connect and start the selected shell without repeating LDN delivery. IZPSX is updated to 0.5.1 with matching corresponding source and native-menu screenshots.

After LDN restoration, loader discovery now uses the selected LAN/Wi-Fi transport instead of scanning only the LDN radio. Pending automatic discovery is shown as Connecting to game loader with an available Stop action; a duplicate manual launch cannot race it.


## IZPSX emulator 0.5.1

RAM/SD source detection, local games/BIOS access, persistent memory cards, shared PC file view and per-player control settings are included in the frozen package. Plus is PS1 Start; Minus opens the IZPSX menu/settings. Follow the module's README for controller layouts and Multitap restrictions. The exact corresponding source accompanies the platform and is prepared separately as a local GitHub-ready project.

## Known limits

FS acquisition can still fail in a new session; its estimate is not a guarantee. Unsafe cleanup is not automatically retried. General app suspension/minimization is not implemented. Component native host gates stay in force. This preparation does not claim new hardware, long-session, audio or game-compatibility results for IZPSX.

Local verification: 105 platform checks passed, one historical audit was skipped, and 26 standalone IZPSX checks passed. Archive checksums, source manifests and a clean Linux x64 install/offline preview were verified. See [Compatibility](COMPATIBILITY.md), [FAQ](FAQ.md) and [local checks](distribution/LOCAL-CHECKS.md) for scope and reproduction.
