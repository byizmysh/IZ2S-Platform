# Build and verify IZ2S for Civilization VII 0.34

This release targets **native Linux x64 / ARM64 only**. Use Node.js 24 and Python 3 for development checks. Component-local production JavaScript dependencies are included. The Git checkout retains application development sources and omits the large Electron binaries included in the Linux installation archive.

This workflow packages existing compatible native components. A complete native rebuild is not claimed; see [SOURCES.md](SOURCES.md).

## Offline checks

```sh
npm run check
node --test tests/release/sd-kit.test.cjs tests/release/stability-defaults.test.cjs
node --test engines/IzCivSwitchFS/tools/session.test.cjs engines/IzCivSwitchFS/tools/iz2s.test.mjs engines/IzCivSwitchFS/tools/persist-verification.test.mjs
```

The CI workflow also runs the Civ7-only navigation, save handling, UI and process/session lifecycle tests. These checks do not contact a console. The verifier checks recorded files, native build input/output bindings, descriptors, eight PC library modules, child-process resources and the four-app SD package. Tests preserve the selected IZA/companion bytes, including the recorded IZPS2 local overlay, reject malformed bundles and check every SD manifest entry and the stability default flag in the separate IZPS2 export.

## Build the SD archive

From the repository root:

```sh
node engines/IzCivSwitchFS/tools/sd-package.mjs
```

This writes `engines/IzCivSwitchFS/dist/iz2sciv-core-sd.zip` and prints its SHA-256, using this checkout's verified runtime and applications. Keep generated output outside the prepared installation tree. For interface review, `node start.cjs --preview --no-open` opens a temporary local port with console actions disabled. Stop preview and exclude its generated state before sealing a release.

## Package the Linux installer

Prepare a separate workspace containing only:

```text
staging/IZ2S-0.34-linux/
```

Use a verified complete release tree, not an installed user's folder. Include both Linux Electron runtime kits and all their notices. Root `data/` and `dist/` must be empty. No keys, pairing, console caches, logs or old sessions belong in staging. The profile's `releaseTarget` is `linux`; Windows installation files and executables must be absent.

Run from the source checkout:

```sh
python3 tools/release/package-prepared.py --workspace /absolute/path/to/release-workspace
```

The tool checks the exact allowed inventory, builds the SD ZIP and embeds it next to `install.sh` inside the Linux tar.gz. It wraps the tar.gz in a ZIP and writes all three verified archives and their SHA-256 checksums under `archives/`. It refuses existing output archives. Hash updates are a deliberate release step after provenance and compatibility review; the tool never changes hashes to bypass failures.

A source snapshot alone does not contain all inputs needed to reconstruct the complete host package or native runtime. See `distribution/application-origins.json`, native provenance and [SOURCES.md](SOURCES.md). Local packaging does not publish to GitHub or verify a console launch. ARM64 host execution must be tested separately from x64 packaging.
