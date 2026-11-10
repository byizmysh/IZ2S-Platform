# Local checks for 0.22

The actual preparation results are recorded in the adjacent local release reports and checksummed artifacts. Run `npm run test:release`, `node distribution/verify.cjs --release` on the clean exported tree, and `node distribution/preview-smoke.cjs <exported-tree>` for offline PC validation.

The release contains the two launcher styles, four console module packages and integrated Super File Bros. It emits source and Linux archives only. Verify SOURCE-MANIFEST.json and SHA256SUMS after the final build. Check that no runtime data, sessions, keys, BIOS/game files, crash dumps, experimental app packages or Windows/macOS artifacts entered the output.

Native PS1 payload/source hashes are pinned separately; repacking it is not a new native rebuild or hardware pass. Physical end-to-end qualification, Linux ARM64 physical runs and long-session checks remain pending unless a matching evidence report explicitly records them. See HOST-TESTING.md and the root COMPATIBILITY.md.
