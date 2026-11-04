# IZ2S Core · 0.21

IZ2S Core is the default connection engine in IZ2S Platform. IZMYSH is the
developer credit; IZlauncher and IZPSlauncher are the console menus.
The existing engine ID `izmysh`, folder and saved BASIC name `NS2IZM` are retained.

## Implementation

The host contract (`runtime/contract.cjs`), adapter, transport, TCP record reader
and FS pipeline process lifecycle have separate implementations. The record
reader assembles fragmented TCP data from bounded chunks instead of repeatedly
concatenating the full receive buffer. The pipeline runner caches one release
operation and reports an unknown IPC outcome without replaying it.

The native protocol, address checks, qualification sequence, FS admission rules,
native function verification and console payload are intentionally preserved.
This is a maintained derivative with a separate host implementation, not a claim
that the underlying console research or native implementation was created anew.

## Upgrade from 0.16

The published 0.16 default used NS2I, port 39395 and nine arguments. This core uses NS2IZM, port 39380 and five arguments. Run First Setup again; renaming the old file is insufficient. Windows/macOS PC installation entry points do not change the current Linux-only native host declarations. See the platform COMPATIBILITY.md for pending tests.

## Compatibility

- TCP port 39380; five START arguments; protocol 1; no NS2R resume.
- Existing `NS2IZM` remains compatible; BASIC SHA-256:
  `5a2b86da8787318652365b2e4cf710f67ce618d6c21087dfc78716533a510d32`.
- Both console menus, optional held FS RPC, Core only mode and the explicit
  firmware-dump CLI retain their existing functions.
- The hardware input API is independent of the engine revision.
- A disconnected native operation is never replayed automatically.

Run `node tools/build-iz2s-core.cjs --zig /path/to/zig` from the platform root.
Zig 0.15.2 recompiles both ARM64 components. The build verifies the packaged
native images and produces `core-build.json`; it does not modify the console.

Host and protocol comparisons are local regression evidence. A full fresh
console launch is still required to qualify this revised host implementation.

## Provenance

The foundation is [BermTheSequel/ns2jb2](https://gitlab.com/BermTheSequel/ns2jb2),
revision `ee87d8301cb317ff0ee1d49bcedcfea5e1d8b04e`. Upstream notices, source
revision and bootstrap hashes remain in the descriptors and `THIRD_PARTY.json`.
See the platform's `LICENSE`, `LICENSE-SCOPE.md` and `COPYRIGHT`.
