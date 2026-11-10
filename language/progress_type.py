"""Proportional progress glyphs, with shared baselines and measured advances."""
from PIL import ImageFont
from language.fonts import font


def paint(draw, first_row):
    top = (first_row + 31) // 32 * 32
    padding = 2
    advances = []
    for weight, (size, name) in enumerate([
            (24, 'DejaVuSans-Bold.ttf'), (21, 'DejaVuSans.ttf')]):
        face = ImageFont.truetype(font(name), size)
        for index in range(96):
            char = chr(index + 32)
            x, y = (index % 32) * 32, top + weight * 96 + (index // 32) * 32
            bounds = face.getbbox(char, anchor='ls')
            if not (0 <= padding + bounds[0] <= padding + bounds[2] <= 32
                    and 0 <= 25 + bounds[1] <= 25 + bounds[3] <= 32):
                raise ValueError('Progress glyph leaves its atlas cell: ' + repr(char))
            # Negative left bearings (notably J/j) must not paint into the
            # preceding character's cell and leave stray pixels in its text.
            draw.text((x + padding, y + 25), char, font=face, fill=255, anchor='ls')
            advances.append(f'{face.getlength(char):.4f}')
    header = ('// Generated proportional DejaVu Sans progress typography.\n'
              + f'const int LOAD_GLYPH_Y={top};\n'
              + f'const int LOAD_GLYPH_X={padding};\n'
              + 'const float LOAD_ADVANCE[192]=float[192](\n'
              + ',\n'.join(','.join(advances[i:i+16])
                          for i in range(0, len(advances), 16)) + '\n);\n')
    return top + 192, header
