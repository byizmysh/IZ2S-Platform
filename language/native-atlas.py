"""Translate baked shell labels without changing shader offsets or buffer sizes."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json, os, re, sys
ROOT = Path(__file__).resolve().parent.parent
kind, shell, language = sys.argv[1:]
messages = json.loads((ROOT / 'language' / language / 'ui.json').read_text())
fonts = ([os.environ.get('HB_CJK_FONT',''), '/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc'] if language == 'zh' else []) + [os.environ.get('HB_FONT',''), str(ROOT / 'language/fonts/DejaVuSans.ttf')]
font = next(item for item in fonts if item and Path(item).is_file())
raw = bytearray(sys.stdin.buffer.read())
def paint(offset, width, height, source, glsl, prefix):
    length = width * height // 4
    original = raw[offset:offset + length]
    if len(original) != length: raise ValueError('Truncated interface atlas')
    pixels = bytes(((byte >> shift) & 3) * 85 for byte in original for shift in (0, 2, 4, 6))
    image = Image.frombytes('L', (width, height), pixels)
    draw = ImageDraw.Draw(image)
    labels = {key: text for key, text in re.findall(r"\('([A-Z_0-9]+)','([^']*)',\d+", source.read_text())}
    rects = {key: tuple(map(int, coords.split(','))) for key, coords in re.findall(r'const ivec4 ' + prefix + r'([A-Z_0-9]+)\s*=\s*ivec4\(([^)]+)\)', glsl.read_text())}
    for key, text in labels.items():
        translated = messages.get(text, text)
        if translated == text or key not in rects: continue
        x, y, w, h = rects[key]
        if min(w, h) <= 0 or x+w > width or y+h > height: raise ValueError('Invalid label rectangle')
        draw.rectangle((x, y, x+w-1, y+h-1), fill=0)
        size = max(6, h+8)
        while True:
            face = ImageFont.truetype(font, size)
            box = face.getbbox(translated)
            if (box[2]-box[0] <= w and box[3]-box[1] <= h) or size <= 6: break
            size -= 1
        # Fit within the existing label rectangle, retaining every shader address.
        tile = Image.new('L', (max(1, box[2]-box[0]), max(1, box[3]-box[1])))
        ImageDraw.Draw(tile).text((-box[0], -box[1]), translated, font=face, fill=255)
        if tile.width > w or tile.height > h: tile.thumbnail((w, h), getattr(Image, 'Resampling', Image).LANCZOS)
        image.paste(tile, (x, y + (h-tile.height)//2))
    pixels = image.tobytes()
    raw[offset:offset+length] = bytes(sum(((pixels[i+j]*3+127)//255) << (2*j) for j in range(4)) for i in range(0, len(pixels), 4))
base = ROOT / 'shells' / shell
if kind == 'legacy':
    meta = json.loads((base / 'server/shaders/launcher-text.json').read_text())
    paint(0, 512, meta['height'], base / 'server/launcher-text.py', base / 'server/shaders/launcher-text.glsl', 'TEXT_')
else:
    meta = json.loads((ROOT / 'shells/izlauncher/app/assets/focus-type.json').read_text())
    paint(0, 768, meta['height'], ROOT / 'core/packages/build-focus-assets.py', base / 'app/shaders/focus-type.glsl', 'F_')
    if shell == 'izpslauncher':
        paint(0x60000-65536, 1024, 2048, base / 'server/ps-assets.py', base / 'app/shaders/ps-text.glsl', 'PS_')
sys.stdout.buffer.write(raw)
