#!/usr/bin/env python3
"""pack_uf2.py - pack a raw firmware .bin into a UF2 image.

Workaround for picotool segfaulting on large images: replicates the exact
block format of picotool's output, verified byte-compatible with
known-good picotool UF2s (both platforms).

Usage: python tools/pack_uf2.py <firmware.bin> <out.uf2> [rp2040|rp2350]

rp2350 (default): abs block + rp2350-arm-s data blocks.
rp2040: rp2040-family data blocks only.
"""
import struct, sys

MAGIC0, MAGIC1, FOOTER = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FAM_ABS, FAM_ARM_S, FAM_RP2040 = 0xE48BFF57, 0xE48BFF59, 0xE48BFF56
EXT_RP2_IGNORE_BLOCK = 0x9957E304  # picotool: UF2_EXTENSION_RP2_IGNORE_BLOCK
FLASH_BASE = 0x10000000


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        sys.exit(1)
    bin_path, out_path = sys.argv[1], sys.argv[2]
    family = sys.argv[3] if len(sys.argv) == 4 else "rp2350"
    if family not in ("rp2040", "rp2350"):
        raise SystemExit(f"unknown family {family!r} (rp2040 or rp2350)")
    data = open(bin_path, "rb").read()
    # picotool zero-pads the image to a 256-byte boundary - match it exactly
    if len(data) % 256:
        data += bytes(256 - len(data) % 256)
    chunks = [data[i:i + 256] for i in range(0, len(data), 256)]

    def mk(flags, addr, psz, bno, nblocks, fam, payload):
        b = bytearray(512)
        struct.pack_into("<IIIIIIII", b, 0, MAGIC0, MAGIC1, flags, addr,
                         psz, bno, nblocks, fam)
        b[32:32 + len(payload)] = payload
        struct.pack_into("<I", b, 508, FOOTER)
        return bytes(b)

    blocks = []
    if family == "rp2350":
        # abs block (RP2350-E10 fix), byte-identical to picotool's
        # gen_abs_block: 0xEF-filled page + RP2-ignore-block extension tag
        # at data[256] (flags 0xA000 = family id | extension flags present).
        abs_payload = b"\xef" * 256 + struct.pack("<I", EXT_RP2_IGNORE_BLOCK)
        blocks.append(mk(0xA000, 0x10FFFF00, 256, 0, 2, FAM_ABS, abs_payload))
    for i, ch in enumerate(chunks):
        blocks.append(mk(0x2000, FLASH_BASE + i * 256, len(ch), i,
                         len(chunks), FAM_ARM_S if family == "rp2350" else FAM_RP2040, ch))
    uf2 = b"".join(blocks)

    # self-verify: every block parses and data round-trips exactly
    out = b""
    for i in range(0, len(uf2), 512):
        m0, m1, fl, ta, psz, bn, nc, fam = struct.unpack_from("<IIIIIIII", uf2, i)
        assert m0 == MAGIC0 and m1 == MAGIC1 and struct.unpack_from("<I", uf2, i + 508)[0] == FOOTER
        if i or family == "rp2040":
            out += uf2[i + 32:i + 32 + psz]
    assert out == data, "round-trip mismatch"
    open(out_path, "wb").write(uf2)
    print(f"{out_path}: {len(uf2)} bytes, {len(uf2)//512} blocks, family {family}, round-trip EXACT")


if __name__ == "__main__":
    main()
