# Source layout and notices

The root tree contains the 0.25 PC panel, runtime JavaScript, installers, descriptors, resources and retained native source files. Production JavaScript dependencies remain next to their owning components, with their licenses. Application development sources excluded from the end-user installers are under `sources/applications/`.

`sources/izpsx/` is extracted from the published IZPSX 0.5.2 source archive with SHA-256 `6fb81fcf01e8db101711528930917adaa4a9ec0a78c1b19323b341af785a687b`. It contains the PCSX-ReARMed vendor snapshot, build files, font and SDK notices, and the `bios.bin` path update. No BIOS or game images are supplied.

`sources/native-cheese/` retains the available native Cheese integration patch and its original build recipe. That recipe still refers to the developer's pinned external build workspace; it is provenance, not a portable full rebuild command.

The native runtime and firmware have inherited build inputs outside this checkout. Historical native menu build records also do not describe every current output. This snapshot is **not a complete corresponding-source archive for the entire native runtime and firmware**, and a clean full native rebuild is not claimed. These gaps are not resolved by including upstream links or license texts. Existing notices and source obligations remain applicable.

The complete Electron runtime kits are distributed in the Linux installation asset; Git contains their source-facing application code and notices but omits the large runtime binaries. They are not needed by the Windows installation asset.

See [LICENSE-SCOPE.md](LICENSE-SCOPE.md), [AUTHORS.md](AUTHORS.md), [THIRD_PARTY.json](THIRD_PARTY.json), and [LICENSES/INDEX.md](LICENSES/INDEX.md). Publishing this snapshot does not relicense third-party code or imply that missing permission has been granted.
