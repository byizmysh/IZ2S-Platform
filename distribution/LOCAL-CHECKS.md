# Local verification record

Candidate: **IZ2S Platform 0.21.0**, reviewed on **2026-11-04**. All work and generated archives remain local. No release, remote workflow, repository push or device firmware update was performed by this review.

## Software checks

| Area | Local result and scope |
| --- | --- |
| Installer logic | 16 regression cases passed: supported architectures, managed runtimes, hashes, missing files, executable permissions, nested destination refusal, interpreter routing and preservation of existing installations |
| Desktop and language policy | 4 cases passed: Windows Path casing, macOS browser selection, empty PATH handling and translation exceptions |
| Module SD directories | 11 cases passed: stable IDs, idempotent creation, existing data, concurrent preparation, mount permissions and error propagation |
| Workspace lifecycle | 11 cases passed: stale owners, panel/session identity, listener reachability, view closure, late responses and shared launcher module contracts |
| Real local settings listeners | 4 cases passed: dead-listener replacement, retained cleanup without replay, cache invalidation after Stop and closing healthy settings despite another failure |
| Package validation | JavaScript/JSON syntax, engine contracts, runtime inventory, all three bundled module packages, ARM/ADV hashes, bundled fonts and compiled SPIR-V validation passed |
| Packaging hygiene | A repeat pre-publication audit found generated Python bytecode in the rebuilt staging tree. The builder now disables bytecode generation, removes residual caches and makes release validation reject them; one new regression case covers cache directories and loose bytecode files |
| English text | Project documentation, default UI text and scripts were checked; the four language catalogs and their translation runtime intentionally contain other languages |

The regression sources stay in the development workspace and are excluded from the prepared public packages. `node distribution/verify.cjs` remains available in an installed copy.

## Installation and browser checks

The rebuilt source manifest and all four archive checksums passed validation. An isolated Linux installation from the prepared ZIP passed in a path containing spaces and a non-ASCII character. The extraction deliberately lost executable bits, and the installer restored them from the manifest. Dependency checks also passed with an inherited environment pointing at a different installation.

A second real installation through the shared managed-runtime installer passed on Linux using private Node.js 24, CPython 3.12 and Pillow 12.0.0. Its receipt distinguishes a declared native host from physical qualification; installation alone does not grant qualification. No system runtime, desktop shortcut, console input or firmware was changed by these trials.

Both installed copies passed offline Preview startup, page loading, health and authenticated API checks. The browser check verified English manual/files previews, App library and switching the new messages between English, Russian, Spanish and Chinese. The previous isolated browser lifecycle check verified view closure, quick reopen, library spacing and removal of Tools when a test session ends. Preview does not type on, flash or issue native commands to a console.

The pre-publication review repeated the original 46 regression cases and added a packaging-hygiene regression case, for 47 cases total, and verified all four archive/file inventories. A fresh installation from the source ZIP into a path containing spaces and an accented character passed dependency checks and offline Preview health/page/authenticated-state checks. Loopback-listener tests were rerun with local-server access after the restricted environment initially refused socket binding; that refusal was not an application regression.

## Limits

Windows x64 and macOS Intel/Apple silicon installation and device workflows still need execution on their actual operating systems. PowerShell parsing and simulated platform plans on Linux cannot establish those results. Native console-session declarations remain Linux-only; Windows ARM64 is excluded.

This review does not replace a fresh physical console run, a long-session test, Linux ARM64 qualification or acceptance testing for every separately installed module. Follow [HOST-TESTING.md](HOST-TESTING.md) and retain the distinctions in [COMPATIBILITY.md](../COMPATIBILITY.md).
