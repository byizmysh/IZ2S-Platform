"""Bind persisted VU offsets to the unrelocated engine code and data."""

from hashlib import sha256
import json
from pathlib import Path
import struct
import subprocess
import sys

elf = Path(sys.argv[1]).resolve()
image = bytearray(elf.read_bytes())
assert image[:7] == b'\x7fELF\x02\x01\x01'
program_offset = struct.unpack_from('<Q', image, 32)[0]
program_size, program_count = struct.unpack_from('<HH', image, 54)
header = struct.Struct('<IIQQQQQQ')
assert program_size == header.size
segments = [header.unpack_from(image, program_offset + i * program_size) for i in range(program_count)]
load = sorted((seg for seg in segments if seg[0] == 1), key=lambda seg: seg[3])
code, data = load
assert code[1] == 5 and data[1] == 6

nm = subprocess.check_output(['llvm-nm', '-S', elf], text=True)
symbols = {}
for line in nm.splitlines():
    row = line.split()
    if len(row) == 4 and (row[-1].startswith('__iz_vu_') or row[-1] == 'iz_vu_cache_image_seal'):
        symbols[row[-1]] = tuple(int(value, 16) for value in row[:2])

for name, segment in (('rx', code), ('rw', data)):
    start = symbols[f'__iz_vu_{name}_start'][0]
    last = symbols[f'__iz_vu_{name}_last'][0]
    assert start == segment[3] and not start % 4096
    assert start <= last < start + segment[6]

address, size = symbols['iz_vu_cache_image_seal']
assert size == 32 and code[3] <= address and address + size <= code[3] + code[5]
position = code[2] + address - code[3]
assert image[position:position + size] == bytes(size), 'Expected an unsealed ELF from the linker'
digest = sha256(b'IZPS2 hosted VU split image v1\0')
for segment in load:
    _, flags, offset, vaddr, _, file_size, memory_size, align = segment
    assert offset + file_size <= len(image)
    digest.update(struct.pack('<IQQQQ', flags, vaddr, file_size, memory_size, align))
    digest.update(image[offset:offset + file_size])
seal = digest.digest()
image[position:position + size] = seal
elf.write_bytes(image)
report = {'seal': seal.hex(), 'fileOffset': position,
          'elfSha256': sha256(image).hexdigest(),
          'segments': {'rx': {'begin': hex(symbols['__iz_vu_rx_start'][0]), 'last': hex(symbols['__iz_vu_rx_last'][0])},
                       'rw': {'begin': hex(symbols['__iz_vu_rw_start'][0]), 'last': hex(symbols['__iz_vu_rw_last'][0])}}}
elf.with_name('vu-image-seal.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
