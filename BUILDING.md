# Building IZ2S Platform 0.22

## Targets and tools

Release archives target Linux x64/ARM64 only. Build on Linux with Node.js 24, Python 3 with Pillow, glslangValidator and spirv-val. Keep bundled native engine/toolchain/license notices intact. ADV firmware is supplied as checked images with its corresponding source; rebuilding it separately requires PlatformIO and the pinned dependencies in its platformio.ini.

```sh
npm run test:release
python3 distribution/release.py --rebuild --output dist/github
```

The builder stages only allowlisted files outside the checkout, rebuilds both console launchers and the selected console app resources, packs the four console IZM modules and ARM input archive, validates the clean tree, and emits:

- `iz2s-platform-0.22.0-source.zip`
- `iz2s-platform-0.22.0-linux.tar.gz`
- `SHA256SUMS`, `SOURCE-MANIFEST.json`, and an extracted `iz2s-platform/` tree

The Linux archive selects the host runtime at installation; it does not embed a Linux desktop binary for every architecture. No Windows or macOS artifact is generated.

The source manifest records every shipped file's hash, size and execute bit. Regenerate it through the release builder after source/resource changes; do not copy an older manifest over a newer checkout. Developer regression tests in `tests/` are kept in Git; installed copies use `node distribution/verify.cjs` and offline preview validation.

## IZPSX corresponding source

The frozen native module and its source must stay together. `sources/izpsx` contains the exact source, vendor snapshot, licenses and ABI profile used for the included 0.5.1 binary, plus build instructions. Rebuilding native PS1 code is separate from the platform launcher's GPU/resource build. A local standalone repo and matching source/archive assets accompany this release.

## Exclusions

No `data/`, session snapshots, secrets, logs, game/BIOS files, private module libraries, build caches or test application packages may enter the public tree. The five listed user-facing apps/tools are the complete bundle selection. Release creation does not install anything on Switch, start a console session or publish/upload artifacts.
