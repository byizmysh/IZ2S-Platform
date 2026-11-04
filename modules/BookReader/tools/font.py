#!/usr/bin/env python3
from PIL import Image,ImageFont,ImageDraw
import sys
sys.path.insert(0,str(__import__("pathlib").Path(__file__).resolve().parents[3]))
from language.fonts import font as resolve_font
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1];out=root/'switch/assets';out.mkdir(parents=True,exist_ok=True)
faces=['DejaVuSerif.ttf','DejaVuSerif-Bold.ttf','DejaVuSans.ttf','DejaVuSans-Bold.ttf'];atlas=Image.new('L',(1024,1920));draw=ImageDraw.Draw(atlas);advances=[]
for face,name in enumerate(faces):
 font=ImageFont.truetype(resolve_font(name),48)
 for index in range(96):
  code=32+index;cell=face*96+index;x=cell%16*64;y=cell//16*80
  draw.text((x+4,y+4),chr(code),font=font,fill=255)
  advances.append(round(font.getlength(chr(code))*64))
(out/'font.bin').write_bytes(atlas.tobytes());(out/'font.json').write_text(json.dumps({'width':1024,'height':1920,'size':48,'advances':advances},separators=(',',':'))+'\n')
# A module-owned icon: open pages, a bookmark and the platform's lime accent.
icon=Image.new('RGBA',(256,256),(0,0,0,0));d=ImageDraw.Draw(icon)
d.rounded_rectangle((12,12,244,244),radius=52,fill='#18211b',outline='#ccfa53',width=4)
d.polygon([(40,64),(120,82),(128,94),(136,82),(216,64),(216,186),(140,202),(128,214),(116,202),(40,186)],fill='#f4f0dd')
d.line([(128,94),(128,205)],fill='#a4af96',width=4)
for y in [105,124,143,162]:
 d.line([(55,y-7),(108,y+5)],fill='#a7b09a',width=4);d.line([(148,y+5),(201,y-7)],fill='#a7b09a',width=4)
d.polygon([(167,63),(188,59),(188,118),(177,111),(167,123)],fill='#b9e83b')
icon.save(out/'icon.png')
