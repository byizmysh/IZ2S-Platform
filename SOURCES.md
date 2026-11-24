# Source layout and notices

The root tree contains the 0.3.2 Civilization VII PC panel, runtime JavaScript, installers, descriptors, resources and retained native source files. Production JavaScript dependencies remain next to their owning components, with their licenses. Application development sources excluded from the end-user installers are under `sources/applications/`.

`sources/izpsx/`, `sources/izps2/` and `sources/applications/iz2s-cheese/` retain the published source trees for IZPSX 0.5.3, IZPS2 0.5.0 and IZ2S Cheese 0.8.7, including original authors, licenses, dependencies and build records. Exact upstream tags and downloaded archive checksums are recorded in `distribution/application-origins.json`. The prebuilt launchers and companions match the official IZA release assets byte for byte; packaging does not rebuild or alter them. The module catalog's shader/native sections and icon are extracted from those same images for library metadata.

The IZPS2 source release records inherited toolchain inputs and states that a complete clean native rebuild is pending. That upstream limitation is preserved. No BIOS or game images are supplied.

`sources/native-cheese/` retains the historical native Cheese integration patch and its original build recipe as provenance for inherited runtime work. The console menu retains its hash-bound Cheese source inputs under `modules/iz2s-cheese/switch/`; they are recorded as inputs for the packaged menu binary, not the separately updated Cheese 0.8.7 IZA.

The native runtime has inherited build inputs outside this checkout. The current menu includes the Civ7-UI shader-cache replacement fix. Removed SB4 installer and input-device firmware files are not part of the current runtime distribution; their earlier source and notices remain in Git history where applicable. The current menu build record binds its recorded sources and outputs by hash. Historical source snapshots for unchanged binaries are retained under `sources/runtime-build/`; other inherited native build records do not describe every current output. This snapshot is **not a complete corresponding-source archive for the entire native runtime and firmware**, and a clean full native rebuild is not claimed. These gaps are not resolved by including upstream links or license texts. Existing notices and source obligations remain applicable.

CivSwitchLDN and CivSwitchFS runtime sources, their original notices, and locally integrated changes remain under `engines/CivSwitchLDN/` and `engines/IzCivSwitchFS/`.

The complete Electron runtime kits are distributed in the Linux installation asset; Git contains their source-facing application code and notices but omits the large runtime binaries. This release contains only a Linux installation asset; Windows installers and bundled Windows host executables are excluded.

See [LICENSE-SCOPE.md](LICENSE-SCOPE.md), [AUTHORS.md](AUTHORS.md), [THIRD_PARTY.json](THIRD_PARTY.json), and [LICENSES/INDEX.md](LICENSES/INDEX.md). Publishing this snapshot does not relicense third-party code or imply that missing permission has been granted.
