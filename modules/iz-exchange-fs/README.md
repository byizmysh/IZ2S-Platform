# File Exchange FS

Choose installed games and start their read-only FTP directly on Switch.
Requires a qualified held FS RPC session: select **IZ2S Core** or **ns2jb by BermTheSequel**, enable
**FS endpoint (--rpc)** in IZ2S before launching, then load this module.

Open **File Exchange FS** on Switch, select **Games (FS RPC)**, choose a game
with A and wait for **FTP ready**. Connect an FTP client to the console address
shown on screen, the port shown on screen (**2122-2199**), anonymous login, passive mode. The game export
contains its available base, updates and DLC; game content is read-only.
Preparation can take several minutes for large titles. The menu keeps rendering.

- A: open a folder or select a game and start its FTP.
- B: parent folder; from Games, disconnect the export and return to title folders.
- X: refresh. L/R: previous/next page. Y: stop/restart the selected game export.
- Minus: return to IZlauncher and close the owned file services.

Outside Games, the original title folders remain available on port 2121:
`/save` and `/tmp` read/write, `/rom` read-only. Game FTP and the Dolphin chooser
share one export owner: selecting another game replaces the previous export;
a stale view cannot stop a newer game. The PC still orchestrates list/mount/setup
through the retained RPC endpoint; this is not a standalone application without
the host. Ordinary native/GPU RPC does not grant full game filesystem access.

The separate module ID allows installation into a live IZ2S session whose
original File Exchange package is already pinned. Its files are not overwritten.
IZ2S uses the retained FS service and OunceCake from `core/filesystem/`; the module keeps the same shared export ownership as Super File Bros.

Build: `node modules/iz-exchange-fs/switch/build.cjs`, then
`node sdk/pack.cjs modules/iz-exchange-fs`.

## Platform status and SD convention

This bundled module currently declares Linux x64/ARM64 hosts. Windows and macOS builds are outside this release scope; physical native-session coverage is not implied by packaging. See the platform COMPATIBILITY.md. Its console SD data location is `SD:/IZ2S/Homebrew/Modules/<module.id>/`; adoption is explicit, and existing data is not migrated automatically. PC-local module data remains private.

Release 0.22 bundles this app with File Exchange, File Exchange FS, Book Reader, IZPSX emulator and integrated Super File Bros. RAM startup requires explicit module selection; no previous selection is restored automatically. See the root README/FAQ for RAM/SD status and shared file-service ownership.
