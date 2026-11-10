# IZ2S Platform 0.22.0

Status: locally prepared release candidate. Publication, GitHub tags/releases and remote workflows have not been run by the local preparation tools.

## Included in this candidate

- IZlauncher now uses the redesigned tile/library presentation, brand mouse, subtle animated background and consistent button hints. IZPSlauncher remains the alternative menu style.
- Connection controls use common engine/mode heading rows, a consistent PC-to-console path and responsive layout. SmileBASIC Launch explicitly points to Make a Program → Direct.
- Civ7 first setup supports Linux Wi-Fi LDN and USB input; subsequent Launch exposes LAN/Wi-Fi selection. PC readiness is independent of initial LDN prerequisites; console-loader readiness remains separate. Selected launchers start automatically after a verified connection.
- Session and file-manager cards share layout and controls. Files & uploads appears after connection. Super File Bros. is integrated for browsing and uploads, with optional FTP for external clients.
- App-library cards show RAM/SD state, filters and installation controls. RAM modules require explicit choice; the old selection is not restored into a different game/session.
- Attempts includes both results, compact entries, date/result/error filtering and history removal. Finished failures are not replayed as new notifications when the panel is reopened.
- Shared file-service handling and checkpoint-based uploads avoid requiring a second Super File Bros. window. Transfer progress/cancellation are visible; resumed uploads verify the chosen source/checkpoint.
- Bundled apps: File Exchange, File Exchange FS, Book Reader, Super File Bros. and IZPSX emulator 0.5.0. Experimental/test builds and unrelated streaming/emulator packages are excluded.
- Release packaging produces source and Linux archives only. Windows and macOS packaging and entry points are not part of 0.22.

## IZPSX emulator 0.5.0

RAM/SD source detection, local games/BIOS access, persistent memory cards, shared PC file view and per-player control settings are included in the frozen package. Plus is PS1 Start; Minus opens the IZPSX menu/settings. Follow the module's README for controller layouts and Multitap restrictions. The exact corresponding source accompanies the platform and is prepared separately as a local GitHub-ready project.

## Known limits

FS acquisition can still fail in a new session; its estimate is not a guarantee. Unsafe cleanup is not automatically retried. General app suspension/minimization is not implemented. Component native host gates stay in force. This preparation does not claim new hardware, long-session, audio or game-compatibility results for IZPSX.

See [Compatibility](COMPATIBILITY.md), [FAQ](FAQ.md) and the generated local verification report for the precise checks performed on this candidate.
