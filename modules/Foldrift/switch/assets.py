from pathlib import Path
import struct
from PIL import Image, ImageDraw, ImageFont

here = Path(__file__).resolve().parent
font_path = here.parents[2] / 'language/fonts/DejaVuSansMono.ttf'
labels = (
    'Foldrift', 'App folder', 'FOLDER', 'L / R Page    Y Paste    + Exit',
    'NAME', 'SIZE', 'A Open    B Parent    X Actions',
    'L / R Page    Y Refresh    + Exit',
    'This folder is empty', 'SD', 'Opening storage...',
    'A Select    B Back', 'A Delete file    B Keep file',
    'L / R Previous / next    B Close',
    'A Type    X Backspace    Y Shift    + Save    B Cancel',
    'B Cancel copy', '+ Return to launcher', 'SD card',
)

glyphs = Image.new('L', (160, 240))
draw = ImageDraw.Draw(glyphs)
face = ImageFont.truetype(str(font_path), 15)
for n, ch in enumerate([*range(32, 127), *range(0x400, 0x460)]):
    draw.text((n % 16 * 10, n // 16 * 20), chr(ch), font=face, fill=255)

table, strings = [], bytearray()
for label in labels:
    table.extend((len(strings), len(label) + 1))
    strings.extend(label.encode('ascii') + b'\0')
keys = b'abcdefghijklmnopqrstuvwxyz0123456789._- '
offset = 16 + 8 * len(labels)
font_offset = offset + len(strings)
header = struct.pack('<4I', len(labels), offset, font_offset, font_offset + 160 * 240)
asset = header + struct.pack('<%dI' % len(table), *table) + strings + glyphs.tobytes() + keys
(here / 'assets/font.bin').write_bytes(asset + b'\0' * ((-len(asset)) % 4))
(here / 'assets/state.bin').write_bytes(bytes(1024))

icon = Image.new('RGBA', (192, 192), (13, 26, 40, 255))
draw = ImageDraw.Draw(icon)
draw.rounded_rectangle((24, 24, 168, 168), radius=27, fill=(24, 45, 65), outline=(105, 233, 200), width=5)
for left in (44, 105):
    draw.rounded_rectangle((left, 67, left + 43, 132), radius=8, fill=(47, 77, 97))
    draw.rounded_rectangle((left + 7, 79, left + 36, 94), radius=4, fill=(107, 231, 199))
    draw.rounded_rectangle((left + 7, 106, left + 31, 112), radius=3, fill=(167, 197, 220))
icon.save(here / 'assets/icon.png')
