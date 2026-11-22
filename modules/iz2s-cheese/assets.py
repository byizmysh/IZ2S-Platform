from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

font_dir, romfs, icon = map(Path, sys.argv[1:])
romfs.mkdir(parents=True, exist_ok=True)

font = bytearray()
scale = 4
for px, bold in ((16, False), (21, False), (26, True), (35, True)):
    path = str(font_dir / ('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf'))
    face = ImageFont.truetype(path, px)
    high_res_face = ImageFont.truetype(path, px * scale)
    widths = bytearray()
    rasters = bytearray()
    for value in range(32, 127):
        glyph = chr(value)
        mask = Image.new('L', (40 * scale, 44 * scale))
        baseline = (1 + face.getmetrics()[0]) * scale
        ImageDraw.Draw(mask).text((scale, baseline), glyph, font=high_res_face, fill=255, anchor='ls')
        widths.append(min(38, round(face.getlength(glyph))))
        rasters.extend(mask.resize((40, 44), Image.Resampling.BOX).tobytes())
    font.extend(widths)
    font.extend(rasters)
(romfs / 'font.bin').write_bytes(font)

with Image.open(Path(__file__).with_name('brand') / 'cheese-icon.png') as source:
    art = source.convert('RGB')
    art.save(icon, quality=97, subsampling=0)
    hardware_icon = art.resize((96, 96), Image.Resampling.LANCZOS).convert('RGBA')
    sidebar_icon = art.resize((44, 44), Image.Resampling.LANCZOS).convert('RGBA')
    (romfs / 'cheese-icon.rgba').write_bytes(hardware_icon.tobytes())
    (romfs / 'brand.rgba').write_bytes(sidebar_icon.tobytes())
