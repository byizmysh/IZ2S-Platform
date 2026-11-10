#!/usr/bin/env python3
"""Prepack a linked ARM64 ELF with emitted relocations; no linker or console.

Only loaded sections are exported. Every allocated relocation is checked
against its linked value, including linker-elided weak calls and implicit GOT
pointers. Unsupported records fail the export instead of guessing addresses.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import zlib

HEADER = struct.Struct('<8sIIQ2Q2I2I3Q2I3I3I144s')
RECORD = struct.Struct('<IHBBQQQ')
assert HEADER.size == 256 and RECORD.size == 32


def align(n, a=4096):
    return (n+a-1) & -a


def signed(n, bits):
    return n-(1 << bits) if n & (1 << (bits-1)) else n


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Elf:
    def __init__(self, file):
        self.raw = Path(file).read_bytes()
        assert self.raw[:7] == b'\x7fELF\x02\x01\x01', 'ELF64 little endian required'
        h = struct.unpack_from('<16sHHIQQQIHHHHHH', self.raw)
        assert h[1:4] == (2, 183, 1), 'Linked ARM64 executable required'
        assert h[8:10] == (64, 56) and h[11] == 64 and 0 < h[12] < 65535
        self.entry = h[4]
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', self.raw, h[6]+i*64) for i in range(h[12])]
        self.programs = [struct.unpack_from('<IIQQQQQQ', self.raw, h[5]+i*56) for i in range(h[10])]
        assert not any(p[0] in (2, 3) for p in self.programs), 'Static image without dynamic loader required'
        self.loads = [p for p in self.programs if p[0] == 1]
        assert len(self.loads) == 2 and [p[1] for p in self.loads] == [5, 6], 'RX and RW loads required'
        assert all(0 < p[3] < 2**39 and p[3] % 4096 == 0 and p[5] <= p[6] <= 128*1024*1024 for p in self.loads)
        self.bases = [p[3] for p in self.loads]
        self.files = [self.data(p[2], p[5]) for p in self.loads]
        self.memory = [align(p[6]) for p in self.loads]
        assert (self.bases[0]+self.memory[0] <= self.bases[1] or
                self.bases[1]+self.memory[1] <= self.bases[0]), 'Loaded segments overlap'
        assert self.bases[0] <= self.entry < self.bases[0]+len(self.files[0])
        for s in self.sections:
            if s[1] != 8:
                self.data(s[4], s[5])
            if s[1] == 9 and self.sections[s[7]][2] & 2:
                raise ValueError('Allocated REL records are unsupported; emitted RELA required')
            if s[1] == 19 and s[2] & 2:
                raise ValueError('Allocated RELR records are unsupported')
        names = self.sections[h[13]]
        self.names = self.data(names[4], names[5])

    def data(self, offset, size):
        assert 0 <= offset <= len(self.raw) and 0 <= size <= len(self.raw)-offset
        return self.raw[offset:offset+size]

    def name(self, s):
        return self.names[s[0]:self.names.index(b'\0', s[0])].decode('utf-8')

    def relative(self, address):
        for i, base in enumerate(self.bases):
            if base <= address <= base+self.memory[i]:
                return i, address-base
        raise ValueError('Target outside loaded image: '+hex(address))

    def place(self, address, width):
        i, off = self.relative(address)
        assert off+width <= len(self.files[i]), 'Relocation outside initialized bytes'
        return i, off, int.from_bytes(self.files[i][off:off+width], 'little')


def pack(elf, native_report=None):
    e = Elf(elf)
    if native_report:
        p = json.loads(Path(native_report).read_text())
        assert p['format'] == 'hblauncher-native-app/1' and p['abi'] == 3 and p['runtime'] == 'linux'
        assert [int(p[k], 0) for k in ['codeBase', 'dataBase']] == e.bases
        assert [p['codeBytes'], p['dataBytes']] == list(map(len, e.files))
        assert p['dataRegionBytes'] == e.loads[1][6]
        assert [p['codeSha256'], p['dataSha256']] == list(map(sha, e.files))
        assert int(p['entry'], 0) == e.entry == e.bases[0] and int(p['spawn'], 0) == e.bases[0]+0x78
        assert all(int.from_bytes(e.files[0][n:n+4], 'little') == 0xd50324df for n in (0, 0x78)), 'HBXL ABI 3 entry trampolines differ'
    rows = []
    types, invariants = collections.Counter(), collections.Counter()
    got_rows = 0

    def add(address, kind, target):
        width = 8 if kind in (1, 3) else 4
        segment, offset, expected = e.place(address, width)
        target_segment, target_offset = e.relative(target)
        if kind in (2, 3, 4) and segment == target_segment:
            invariants['same-segment-relative'] += 1
            return
        rows.append((offset, kind, segment, target_segment, expected, target_offset, 0))

    def absolute(address, value, symbol_section=None):
        try:
            e.relative(value)
        except ValueError:
            assert value == 0 or symbol_section == 0xfff1, 'Unresolved absolute pointer'
            invariants['constant-absolute'] += 1
        else:
            add(address, 1, value)

    for rel in e.sections:
        if rel[1] != 4 or not e.sections[rel[7]][2] & 2:
            continue
        assert rel[9] == 24 and rel[5] % 24 == 0
        sy = e.sections[rel[6]]
        assert sy[1] == 2 and sy[9] == 24 and sy[5] % 24 == 0
        symbols = [struct.unpack_from('<IBBHQQ', e.raw, sy[4]+i*24) for i in range(sy[5]//24)]
        for at in range(rel[4], rel[4]+rel[5], 24):
            address, info, addend = struct.unpack_from('<QQq', e.raw, at)
            kind = info & 0xffffffff
            types[kind] += 1
            if kind == 0:
                invariants['none'] += 1
                continue
            symbol = symbols[info >> 32]
            assert symbol[3] != 0 or symbol[1] >> 4 != 1, 'Unresolved global symbol'
            target = symbol[4]+addend
            width = 8 if kind in (257, 260) else 4
            segment, offset, word = e.place(address, width)
            if kind == 257:
                assert word == target, 'ABS64 differs from symbol'
                absolute(address, word, symbol[3])
            elif kind in (260, 261):
                assert address+signed(word, width*8) == target, 'PREL differs from symbol'
                add(address, 3 if kind == 260 else 2, target)
            elif kind in (275, 311):
                assert word & 0x9f000000 == 0x90000000, 'ADRP instruction required'
                page = (address & -4096)+(signed((word >> 29 & 3) | ((word >> 5 & 0x7ffff) << 2), 21) << 12)
                if kind == 275:
                    assert page == target & -4096, 'ADRP differs from symbol'
                add(address, 4, page)
            elif kind in (282, 283):
                if kind == 283 and word == 0xd503201f:
                    assert symbol[3] == 0 and symbol[1] >> 4 == 2 and target == 0
                    invariants['undefined-weak-call-nop'] += 1
                else:
                    assert word & 0xfc000000 == (0x94000000 if kind == 283 else 0x14000000)
                    assert address+(signed(word & 0x3ffffff, 26) << 2) == target
                    assert e.relative(target)[0] == segment, 'Cross-segment branch unsupported'
                    invariants['same-segment-branch'] += 1
            elif kind == 277:
                assert word & 0x7f000000 == 0x11000000 and not word & (1 << 22)
                assert word >> 10 & 4095 == target & 4095
                invariants['page-aligned-low12'] += 1
            elif kind in (278, 284, 285, 286, 299):
                scale = {278: 0, 284: 1, 285: 2, 286: 3, 299: 4}[kind]
                assert word & 0x3b000000 == 0x39000000
                assert ((word >> 10 & 4095) << scale) == target & 4095
                invariants['page-aligned-low12'] += 1
            elif kind == 312:
                assert word & 0xffc00000 == 0xf9400000
                invariants['page-aligned-got-low12'] += 1
            elif kind in (264, 266, 268, 269):
                shift = {264: 0, 266: 16, 268: 32, 269: 48}[kind]
                assert word & 0xff800000 in (0xd2800000, 0xf2800000)
                assert (word >> 21 & 3)*16 == shift and word >> 5 & 65535 == target >> shift & 65535
                try:
                    e.relative(target)
                except ValueError:
                    assert symbol[3] == 0xfff1, 'MOVW target outside module'
                    invariants['constant-movw'] += 1
                else:
                    add(address, 5, target)
            else:
                raise ValueError('Unsupported allocated ARM64 relocation: '+str(kind))

    # Static linkers synthesize GOT contents without .rela.got records. These
    # pointers need rebasing as well; instruction low12 fields stay unchanged.
    for s in e.sections:
        name = e.name(s)
        if name in ('.got', '.got.plt'):
            assert s[2] & 2 and s[5] % 8 == 0
            for offset in range(0, s[5], 8):
                value = int.from_bytes(e.data(s[4]+offset, 8), 'little')
                absolute(s[3]+offset, value)
                got_rows += bool(value)
        elif name == '.eh_frame_hdr':
            raw = e.data(s[4], s[5])
            assert raw[:4] == bytes([1, 0x1b, 3, 0x3b]), 'Unrecognized unwind header encoding'
            frame, count = struct.unpack_from('<iI', raw, 4)
            assert len(raw) == 12+count*8 and e.relative(s[3]+4+frame)[0] == 0
            for code, fde in struct.iter_unpack('<ii', raw[12:]):
                assert e.relative(s[3]+code)[0] == e.relative(s[3]+fde)[0] == 0
            invariants['same-segment-unwind-header'] += 1
    assert types, 'ELF must contain emitted allocated relocations'
    rows.sort(key=lambda r: (r[2], r[0]))
    end = [0, 0]
    for row in rows:
        assert row[0] >= end[row[2]], 'Overlapping relocation records'
        end[row[2]] = row[0]+(8 if row[1] in (1, 3) else 4)
    records = b''.join(RECORD.pack(*row) for row in rows)
    sections = [*e.files, records]
    offsets, length = [], 256
    for data in sections:
        length = align(length, 8)
        offsets.append(length)
        length += len(data)
    assert length <= 80*1024*1024
    # HBXL spawn is exported for its thread trampoline. Generic fixtures use
    # entry for both; neither field authorizes calling a foreign native ABI.
    spawn = 0x78 if native_report else e.entry-e.bases[0]
    header = HEADER.pack(b'IZNATRL1', 1, 256, length, *e.bases, *map(len, e.files), *e.memory,
                         *offsets, len(rows), 32, *[zlib.crc32(b) for b in sections],
                         e.entry-e.bases[0], spawn, 0, bytes(144))
    output = bytearray(length)
    output[:256] = header
    for offset, data in zip(offsets, sections):
        output[offset:offset+len(data)] = data
    report = dict(format='iz-relocatable-native-export-v1', passed=True, elfSha256=sha(e.raw),
                  bytes=length, sha256=sha(output), sourceBases=e.bases, fileBytes=list(map(len, e.files)),
                  memoryBytes=e.memory, originalSectionSha256=list(map(sha, e.files)), records=len(rows),
                  entryOffset=e.entry-e.bases[0], spawnOffset=spawn,
                  recordKinds=dict(collections.Counter(row[1] for row in rows)), allocatedRelocationTypes=dict(types),
                  invariantRelocations=dict(invariants), implicitGotPointers=got_rows,
                  allAllocatedRelocationsChecked=True, linkedBytesPreserved=True,
                  nativeExecuted=False, consoleCommands=0, hardwareQualified=False,
                  limitation='Engine image only; PS2 startup/context, resource leases and supervisor handoff remain separate requirements')
    return output, report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', required=True)
    p.add_argument('--native-report')
    p.add_argument('--output', required=True)
    args = p.parse_args()
    output, report = pack(args.elf, args.native_report)
    destination = Path(args.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(output)
    report['output'] = str(destination.resolve())
    destination.with_suffix(destination.suffix+'.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
