#!/usr/bin/env python3
"""Remove code-to-data distance limits from a packed IZPS2 ARM64 image."""

import argparse
from collections import Counter
from pathlib import Path
import struct
import zlib


HEADER = struct.Struct('<8sIIQ2Q4I3Q8I144s')
RECORD = struct.Struct('<IHBBQQQ')
PAGE = 4096
ABS64, PREL32, PREL64, ADRP, MOVW = range(1, 6)


def align(value, alignment):
    return (value + alignment - 1) & -alignment


def displacement(source, destination, bits):
    delta = destination - source
    if delta % 4 or not -(1 << (bits + 1)) <= delta < (1 << (bits + 1)):
        raise ValueError(f'ARM64 displacement out of range at {source:#x}')
    return (delta // 4) & ((1 << bits) - 1)


def convert(image):
    header = list(HEADER.unpack_from(image))
    magic, version, size, total, code_base, data_base = header[:6]
    code_size, data_size, code_memory, data_memory = header[6:10]
    code_at, data_at, records_at = header[10:13]
    count, record_size, code_crc, data_crc, records_crc, entry, spawn, flags, padding = header[13:]
    if ((magic, version, size, total, record_size) !=
            (b'IZNATRL1', 1, HEADER.size, len(image), RECORD.size)
            or flags or any(padding) or count > 1048576
            or code_at != HEADER.size or data_at != align(code_at + code_size, 8)
            or records_at != align(data_at + data_size, 8)
            or total != records_at + count * RECORD.size
            or not code_base or code_base % PAGE or data_base % PAGE
            or not code_size or code_size > code_memory or code_memory % PAGE
            or not data_memory or data_size > data_memory or data_memory % PAGE
            or code_base + code_memory > data_base
            or data_base + data_memory > 1 << 39
            or entry >= code_size or spawn >= code_size or (entry | spawn) % 4):
        raise ValueError('Invalid IZPS2 packed image layout')
    code = bytearray(image[code_at:code_at + code_size])
    data = image[data_at:data_at + data_size]
    records = image[records_at:]
    if tuple(map(zlib.crc32, (code, data, records))) != (code_crc, data_crc, records_crc):
        raise ValueError('IZPS2 packed image checksum mismatch')

    kept, sites = [], []
    last = (0, 0)
    for row in RECORD.iter_unpack(records):
        offset, kind, segment, target_segment, expected, target, reserved = row
        width = 8 if kind in (ABS64, PREL64) else 4
        if (kind not in range(ABS64, MOVW + 1) or segment > 1 or target_segment > 1
                or reserved or (segment, offset) < last
                or offset + width > (code_size, data_size)[segment]
                or target > (code_memory, data_memory)[target_segment]):
            raise ValueError(f'Invalid packed relocation at {segment}:{offset:#x}')
        last = (segment, offset + width)
        if segment != target_segment and kind == PREL32:
            raise ValueError(f'Cross-segment PREL32 at {offset:#x}; keep its read-only target in RX')
        if segment == target_segment or kind != ADRP:
            kept.append(row)
            continue
        if (segment != 0 or offset % 4 or expected >> 32 or (expected & 0x9f000000) != 0x90000000
                or struct.unpack_from('<I', code, offset)[0] != expected or (expected & 31) == 31):
            raise ValueError(f'Unsupported cross-segment ADRP at {segment}:{offset:#x}')
        sites.append((offset, expected & 31, target & -PAGE))

    # Each ADRP becomes a direct branch to a thunk. The thunk loads the
    # relocated page address into the original destination and branches
    # back. Neither the stack, LR, NZCV nor any other register changes.
    if sites:
        code.extend(bytes(align(code_size, 8) - code_size))
    page_slots = {}
    for page in sorted({page for _, _, page in sites}):
        place = len(code)
        value = data_base + page
        code += struct.pack('<Q', value)
        kept.append((place, ABS64, 0, 1, value, page, 0))
        page_slots[page] = place

    for offset, register, page in sites:
        thunk = len(code)
        branch = 0x14000000 | displacement(offset, thunk, 26)
        load = 0x58000000 | (displacement(thunk, page_slots[page], 19) << 5) | register
        back = 0x14000000 | displacement(thunk + 4, offset + 4, 26)
        struct.pack_into('<I', code, offset, branch)
        code += struct.pack('<II', load, back)

    code_memory = max(code_memory, align(len(code), PAGE))
    if code_memory > 128 * 1024 * 1024 or code_base + code_memory > data_base:
        raise ValueError('Not enough RX space for the portable image thunks')
    kept.sort(key=lambda row: (row[2], row[0]))
    records = b''.join(RECORD.pack(*row) for row in kept)
    data_at = align(HEADER.size + len(code), 8)
    records_at = align(data_at + len(data), 8)
    total = records_at + len(records)
    header[3] = total
    header[6], header[8] = len(code), code_memory
    header[11:14] = data_at, records_at, len(kept)
    header[15:18] = map(zlib.crc32, (code, data, records))
    output = b''.join((HEADER.pack(*header), code, bytes(data_at - HEADER.size - len(code)),
                       data, bytes(records_at - data_at - len(data)), records))
    return output, len(sites), Counter(row[1] for row in kept)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result, lowered, relocations = convert(args.source.read_bytes())
    args.output.write_bytes(result)
    print(f'Lowered {lowered} ADRP instructions; {len(result)} bytes; {dict(relocations)}')


if __name__ == '__main__':
    main()
