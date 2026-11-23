# Working with IZ2S Platform 0.3.1

## Install

Use the Linux or Windows installation archive linked from [README.md](README.md). The source checkout omits the large Electron runtime binaries. The release archives are separate sealed installation trees; copying this checkout is not the same as unpacking either installer.

## Check the source snapshot

Use Node.js 24 and Python 3. Production JavaScript dependencies needed by the verifier are included next to their components.

```sh
npm run check
```

This checks the recorded source/runtime files, seven packaged modules, descriptors, native artifact hashes, installer resources, and the six-app Civ7 SD kit. It does not rebuild the native console runtime or issue commands to a console.

## Repackage the prepared installers

Create a separate folder with `staging/IZ2S-Platform-0.3.1-linux/` and `staging/IZ2S-Platform-0.3.1-windows-x64/`, populated by extracting the corresponding full installation assets. Both `data/` and `dist/` must remain empty. Do not copy an installed user's state into staging. Then run from this checkout:

```sh
python3 tools/release/package-prepared.py --workspace /absolute/path/to/release-workspace
```

The command verifies both prepared trees, emits deterministic archives in `archives/`, and writes their SHA-256 checksums. The Linux folder must contain the complete Electron kits from the release, including all licenses and manifests. A modified prepared tree requires an intentional inventory update and new installation verification; this command does not bypass an integrity failure.

## Native builds

See [SOURCES.md](SOURCES.md) for the exact scope and remaining gaps. Application sources and their build recipes are under `sources/applications/`; IZPSX and IZPS2 have their own `sources/izpsx/BUILDING.md` and `sources/izps2/BUILDING.md`. No claim is made that the entire native runtime can be rebuilt from this checkout alone. Installer packaging does not run native compilers, install anything on Switch, or publish to GitHub.
