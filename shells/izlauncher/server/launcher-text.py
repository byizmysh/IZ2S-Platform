"""IZ launcher: compact 2-bit coverage atlas, packed and compressed on disk."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import os, json, zlib, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from language.fonts import font as resolve_font
root=Path(__file__).resolve().parent/'shaders'
width=512
atlas=Image.new('L',(width,512));draw=ImageDraw.Draw(atlas)
font=resolve_font()
mono=resolve_font('DejaVuSansMono.ttf')
labels=[('TITLE','IZlauncher 0.21',48),
 ('NOTITLES','A little room for ideas.',30),('EMPTYHINT','Add an app. Make something small.',18),
 ('SELECT','Open',19),('BACK','Back',19),('EMPTY','App unavailable',30),
 ('DETAIL','Restart the launcher and try again.',18),('CLOSE','Press B to return',18),
 ('BUTTON_A','A',16),('BUTTON_B','B',16),('CREDIT','Created by IZMYSH (@byizmysh)',20)]
y=2;header=['// Generated compact 2-bit IZ font atlas.','const int IZ_ATLAS_WIDTH = 512;']
for name,text,size in labels:
 face=ImageFont.truetype(font if name in ('TITLE','NOTITLES','EMPTY') else mono,size)
 box=draw.textbbox((0,0),text,font=face);w,h=box[2]-box[0],box[3]-box[1]
 assert w+4<=width and y+h+2<=512,(name,w,y+h)
 draw.text((2-box[0],y-box[1]),text,fill=255,font=face)
 header.append(f'const ivec4 TEXT_{name} = ivec4(2,{y},{w},{h});');y+=h+5
height=(y+3)//4*4;atlas=atlas.crop((0,0,width,height))
pixels=atlas.tobytes();packed=bytearray(len(pixels)//4)
for i,value in enumerate(pixels):packed[i//4]|=((value*3+127)//255)<<((i%4)*2)
(root/'launcher-text.bin').write_bytes(packed)
(root/'launcher-text.deflate').write_bytes(zlib.compress(packed,9))
(root/'launcher-text.glsl').write_text('\n'.join(header)+'\n')
(root/'launcher-text.json').write_text(json.dumps({'width':width,'height':height,'bits':2,'bytes':len(packed)},separators=(',',':'))+'\n')
atlas.save(root/'launcher-text.png')
print(f'IZ font: {len(packed)} bytes GPU coverage, {(root/"launcher-text.deflate").stat().st_size} bytes compressed')
