"""Lay out the RUN25 IZPS2 sprite using the IZPSX tile colors and label."""
from hashlib import sha256
from pathlib import Path
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ICON_START = 65536
ICON_END = 102400
TILE_SIZE = 96
TITLE = "IZPS2"

font_dir, input_path, output_path = map(Path, sys.argv[1:])
tile = bytearray(input_path.read_bytes())
assert len(tile) == 131072
source = bytes(tile[ICON_START:ICON_END])
assert sha256(source).hexdigest() == "b42487b8f8abb398da816875ce94a06067cd572883d41ff639986340f2b20465"
sprite = Image.frombytes('RGBA', (TILE_SIZE, TILE_SIZE), source)
left, top, right, bottom = sprite.getbbox()
sprite = sprite.crop((left, top, right, bottom))

# Match the opaque IZPSX artwork plate. Move the original sprite as-is,
# centered in the area above the caption, without re-rasterizing or scaling it.
artwork = Image.new('RGBA', (TILE_SIZE, TILE_SIZE), (11, 16, 32, 255))
position = ((TILE_SIZE - sprite.width) // 2, 33 - sprite.height // 2)
artwork.alpha_composite(sprite, position)

background = artwork.copy()

# The original IZPSX wordmark is DejaVu Sans Bold, 76px on a 512px
# canvas. Render the caption separately so resampling never affects the sprite.
caption = Image.new('L', (2048, 2048))
draw = ImageDraw.Draw(caption)
font = ImageFont.truetype(font_dir / 'DejaVuSans-Bold.ttf', 76 * 4)
x0, _, x1, _ = draw.textbbox((0, 0), TITLE, font=font)
draw.text(((2048 - (x1 - x0)) / 2 - x0, 323 * 4), TITLE, fill=255, font=font)
caption = caption.resize((512, 512), Image.Resampling.LANCZOS)
caption = caption.resize((TILE_SIZE, TILE_SIZE), Image.Resampling.LANCZOS)
assert caption.getbbox()[1] > position[1] + sprite.height
artwork.paste((243, 246, 251, 255), mask=caption)
assert artwork.crop((*position, position[0] + sprite.width,
                    position[1] + sprite.height)).tobytes() == background.crop(
                        (*position, position[0] + sprite.width, position[1] + sprite.height)).tobytes()

tile[ICON_START:ICON_END] = artwork.tobytes()

# Also set the label that IZlauncher renders below the square artwork.
title = Image.new('L', (512, 64))
draw = ImageDraw.Draw(title)
font = ImageFont.truetype(font_dir / 'DejaVuSans.ttf', 34)
x0, y0, x1, y1 = draw.textbbox((0, 0), TITLE, font=font)
draw.text((-x0, -y0), TITLE, fill=255, font=font)
tile[:512 * 64] = title.tobytes()
struct.pack_into('<2I', tile, ICON_END, x1 - x0, y1 - y0)
output_path.write_bytes(tile)
