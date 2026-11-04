# File Exchange

## Quick start

This is the only module bundled in the prepared public release. Load it into a live console session, open its tile on Switch, then connect your FTP client to the console's current IP on port 2121. Use anonymous login, passive mode and one simultaneous connection. The browser panel's 127.0.0.1 address is not the console FTP address. Closing the app closes the service.

[Main README](../../README.md) · [Plain-English FAQ](../../FAQ.md)

File Exchange is a standalone IZlauncher module. Install `iz-exchange.izm` in
`modules/iz-exchange/`, select it in the panel library, then open **File Exchange**
on the console. There is no separate deployment script or automatic FTP server
at platform startup. The declared `switch/` files run on the console; the
module's `server/runtime.cjs` owns its host service through
`hostRuntime: module:iz-exchange/runtime`.

Opening the app starts its console FTP listener on port **2121**. Connect to the
console's current IP using anonymous login, passive mode and one simultaneous
connection. `/save` and `/tmp` permit upload, download, rename, creation and
deletion. `/rom` is read only. These are SmileBASIC's title-owned mounts, not the
PC filesystem or unrestricted access to the console. Uploads replace files
directly; interrupted uploads can leave partial files.

A opens a folder, B opens its parent, X refreshes, L/R change pages, Y toggles
FTP, and Minus returns to IZlauncher. Returning to the menu closes this module's
listener and files. Reopening the app creates a fresh service. Its transfer pump
runs independently of graphics; `pending()` requests a safe render boundary,
and `afterBatch()` publishes response bytes without overwriting GPU requests.

Repack source changes with `node sdk/pack.cjs modules/iz-exchange`. Local tests
cover the FTP state machine, writable/read-only path boundaries, 64-bit file
sizes, mailbox ownership and service cleanup. They do not establish current
physical-console transfer speed or access to game storage.

## Optional game filesystem access

`server/ouncecake/` contains unmodified
[misshexa66/OunceCake](https://github.com/misshexa66/OunceCake), revision
`6fcf9e80d302ff43c96dd5d912c9b08a11913daf`, under GPL-2.0. Its README and LICENSE
remain alongside its source. It is a separate tool with different prerequisites
from the normal SmileBASIC file browser.

The public `games` component exports `availability(capabilities)`,
`preflight(run, options?)`, `list(run)` and `main(run, title?)`. Game storage
requires the `held-filesystem-rpc` capability: an existing qualified session
that acquired and retained a responding filesystem endpoint. IZMYSH does not
provide this capability, so game storage is unavailable in this release.
Normal `/save`, `/tmp` and `/rom` access continues to work independently.

The retained adapter accepts an absolute session directory and checks the
acquisition report, promoted root handle and responding FS socket before
invoking OunceCake. Passing `capabilities` to `preflight` rejects an unsupported
engine before attempting a connection. Its wrapper uses FTP port **2122**;
running upstream OunceCake directly retains its own defaults.

Enabling a setting cannot add FS acquisition to an already running ordinary
IZlauncher session. Do not assume the GPU menu and a separately held FS session
can run together. Previous acquisition attempts did not establish reliable game
FS readiness; an engine failure or retained native resources requires a fresh
console process before another attempt. Game storage access is experimental,
and the normal read/write `/save` and `/tmp` functionality does not prove it.

## Platform status and SD convention

This bundled module currently declares Linux x64/ARM64 hosts. Windows/macOS platform installers do not qualify this module for those hosts; OS/native-session testing is pending. See the platform COMPATIBILITY.md. Its future console SD data location is `SD:/IZ2S/Homebrew/Modules/<module.id>/`; adoption is explicit, and existing data is not migrated automatically. PC-local module data remains private.
