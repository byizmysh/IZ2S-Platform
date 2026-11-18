"""Rasterize package labels/icons into a fixed GPU catalog buffer."""
import json, struct, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

def default_icon():
    tile = Image.new('RGBA', (96, 96))
    draw = ImageDraw.Draw(tile)
    draw.rounded_rectangle((9, 9, 86, 86), radius=16, fill='#203652', outline='#54769d', width=2)
    draw.polygon([(31, 24), (57, 24), (65, 32), (65, 70), (31, 70)], fill='#afcef2')
    draw.polygon([(57, 24), (57, 33), (65, 33)], fill='#607c9d')
    for y in (43, 51, 59):
        draw.line((38, y, 58, y), fill='#3b577a', width=2)
    return tile

apps=json.loads(Path(sys.argv[1]).read_text())
assert len(apps)<=32
slots=bytearray(32*131072)
import os
font=os.environ.get('HB_FONT',str(Path(__file__).resolve().parents[2]/'language/fonts/DejaVuSans.ttf'))
for i,app in enumerate(apps):
    image=Image.new('L',(512,128)); draw=ImageDraw.Draw(image); sizes=[]
    for text,size,y in [(app['name'],34,0),(app['description'],18,64)]:
        face=ImageFont.truetype(font,size)
        while draw.textbbox((0,0),text,font=face)[2]>508:
            text=text[:-2]+'…'
        box=draw.textbbox((0,0),text,font=face)
        draw.text((-box[0],y-box[1]),text,fill=255,font=face)
        sizes.extend([box[2]-box[0],box[3]-box[1]])
    base=i*131072
    slots[base:base+65536]=image.tobytes()
    if app['icon']:
        with Image.open(app['icon']) as icon:
            assert icon.width<=4096 and icon.height<=4096
            icon=ImageOps.contain(icon.convert('RGBA'),(96,96),getattr(Image, 'Resampling', Image).LANCZOS)
        tile=Image.new('RGBA',(96,96));tile.alpha_composite(icon,((96-icon.width)//2,(96-icon.height)//2))
    else:
        tile=default_icon()
    slots[base+65536:base+102400]=tile.tobytes()
    struct.pack_into('<5I',slots,base+102400,*sizes,1 if app.get('displayOnly') else int(app.get('scene') or 0))
    struct.pack_into('<I',slots,base+102428,int(app.get('launchCommand') or 0))
    # Presentation only: never change the built-in launch scene at 102416.
    struct.pack_into('<I',slots,base+102508,int(app.get('category') or 0))
    if app.get('previewOnly'):
        struct.pack_into('<I',slots,base+102520,0x314F524E)  # NRO1
Path(sys.argv[2]).write_bytes(slots)
