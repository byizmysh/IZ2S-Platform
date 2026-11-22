"""Pack the shared Store artwork into the IZA controls data section."""
from pathlib import Path
import sys

romfs, destination = map(Path, sys.argv[1:])
icons = sorted((romfs / 'icons').glob('*.rgba'))
assert len(icons) <= 255
with destination.open('wb') as out:
    out.write(b'IZSTOREA' + bytes([len(icons), 0, 0, 0]))
    out.write((romfs / 'font.bin').read_bytes())
    out.write((romfs / 'brand.rgba').read_bytes())
    for icon in icons:
        label = icon.stem.encode('ascii')
        pixels = icon.read_bytes()
        assert 0 < len(label) < 48 and len(pixels) == 96 * 96 * 4
        out.write(label.ljust(48, b'\0'))
        out.write(pixels)
assert destination.stat().st_size <= 0x200000 - 1024
