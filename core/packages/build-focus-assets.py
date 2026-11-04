"""Small code-native ORBIT FOCUS type atlas and vector app icons."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json,zlib,sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from language.fonts import font
from language.progress_type import paint as progress_type
R=Path(__file__).resolve().parents[2];S=R/'shells/izlauncher/app';FONT=font();MONO=font('DejaVuSansMono.ttf');BOLD=font('DejaVuSans-Bold.ttf')
labels=[('TITLE','IZlauncher',76),('VERSION','0.21',20),('READY','Ready to open',34),('PLAY','Ready to play',34),('ADD','Add package',62),('BROWSE','Browse your package library',22),('OPEN','Open',23),('LOAD','Load package',23),('ADDSMALL','Add app',20),('LIBRARY','YOUR LIBRARY',17),('PACKAGES','PACKAGE LIBRARY',17),('LOADED','Loaded successfully',19),('SUCCESS','Package loaded',20),('WORKING','Loading package',20),('EMPTY','Press X to add an app',22),('ALL','All',23),('GAMES','Games',23),('TOOLS','Tools',23),('SEARCH','Search',23),('Y','Y',22),('X','X',18),('NORESULTS','No matches',55),('SEARCHHELP','Try another name or filter',22),('FIND','Find an app',27),('DONE','Done',19),('CLEAR','Clear',17),('SPACE','Space',17),('CREDIT','Created by IZMYSH (@byizmysh)',21),('BACK','Back',23),('INSTALL','Load',23),('BROWSEBTN','Browse',23),('ASELECT','A Select',18),('BBACK','B Back',18)]
im=Image.new('L',(768,1408));d=ImageDraw.Draw(im);y=2;header=['// ORBIT FOCUS: 2-bit coverage in the original atlas free area.','const uint FOCUS_TYPE=16384u;','const uint FOCUS_WIDTH=768u;']
for name,text,size in labels:
 f=ImageFont.truetype(BOLD if name=='TITLE' else MONO if name in ('LIBRARY','PACKAGES','READY','PLAY','CREDIT') else FONT,size);b=d.textbbox((0,0),text,font=f);h=b[3]-b[1];tracking=3 if name=='CREDIT' else 2 if name in ('READY','PLAY') else 0
 w=round(sum(f.getlength(ch) for ch in text)+tracking*(len(text)-1)) if tracking else b[2]-b[0];assert w<764
 if tracking:
  x=2
  for ch in text:d.text((x,y-b[1]),ch,font=f,fill=255);x+=f.getlength(ch)+tracking
 else:d.text((2-b[0],y-b[1]),text,font=f,fill=255)
 header.append(f'const ivec4 F_{name}=ivec4(2,{y},{w},{h});');y+=h+5

for n in range(11):
 text=str(n) if n<10 else '/';f=ImageFont.truetype(MONO,20);b=d.textbbox((0,0),text,font=f);w,h=b[2]-b[0],b[3]-b[1];d.text((2-b[0],y-b[1]),text,font=f,fill=255);header.append(f'const ivec4 F_NUM_{n}=ivec4(2,{y},{w},{h});');y+=h+4
header.append(f'const ivec4 F_ASCII=ivec4(0,{y},24,32);')
f=ImageFont.truetype(FONT,24)
for i in range(96):
 text=chr(i+32);b=d.textbbox((0,0),text,font=f);x=(i%32)*24;yy=y+(i//32)*32;d.text((x-b[0],yy-b[1]),text,font=f,fill=255)
y+=96
height=(y+3)//4*4;im=im.crop((0,0,768,height));raw=im.tobytes();packed=bytearray(len(raw)//4)
for i,v in enumerate(raw):packed[i//4]|=((v*3+127)//255)<<((i%4)*2)
assert len(packed)+65536<0x1c0000
glyphs=Image.new('L',(1024,192));_,progress_header=progress_type(ImageDraw.Draw(glyphs),0)
glyph_pixels=glyphs.tobytes();glyph_packed=bytearray(len(glyph_pixels)//4)
for i,v in enumerate(glyph_pixels):glyph_packed[i//4]|=((v*3+127)//255)<<((i%4)*2)
combined=bytearray(0x60000-65536+len(glyph_packed));combined[:len(packed)]=packed;combined[0x60000-65536:]=glyph_packed
(S/'shaders/load-progress-type.glsl').write_text(progress_header)
(S/'assets/focus-type.deflate').write_bytes(zlib.compress(combined,9));(S/'assets/focus-type.json').write_text(json.dumps({'width':768,'height':height,'bytes':len(combined),'offset':65536}));(S/'shaders/focus-type.glsl').write_text('\n'.join(header)+'\n');im.save(S/'assets/focus-type.png')
# Original vector silhouettes, rendered at 3x for compact antialiased app icons.
ink='#f2f1e8';design=R/'core/packages/design/icons';design.mkdir(parents=True,exist_ok=True)
skull=[(27,23),(39,15),(62,15),(75,25),(80,42),(75,59),(66,63),(66,79),(59,85),(41,85),(34,79),(34,63),(25,58),(20,42)]
lambda_poly=[(34,17),(48,17),(72,75),(59,81),(45,45),(34,77),(19,77),(38,31)]
for name in ['doom','half-life']:
 image=Image.new('RGBA',(288,288));dr=ImageDraw.Draw(image)
 def poly(v,fill):dr.polygon([(x*3,y*3) for x,y in v],fill=fill)
 if name=='doom':
  poly(skull,ink);poly([(27,38),(45,44),(42,52),(30,50)],'#00000000');poly([(73,38),(55,44),(58,52),(70,50)],'#00000000');poly([(50,51),(43,62),(57,62)],'#00000000')
  for x in [43,52]:dr.rectangle((x*3,68*3,(x+4)*3,88*3),fill='#00000000')
  svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path fill="'+ink+'" fill-rule="evenodd" d="M27 23 39 15 62 15 75 25 80 42 75 59 66 63 66 79 59 85 41 85 34 79 34 63 25 58 20 42Z M27 38 45 44 42 52 30 50Z M73 38 55 44 58 52 70 50Z M50 51 43 62 57 62Z M43 68V89H47V68Z M52 68V89H56V68Z"/></svg>'
  target=design/'doom.webp'
 else:
  dr.ellipse((6*3,6*3,91*3,91*3),outline=ink,width=4*3);poly(lambda_poly,ink);dr.line([(69*3,24*3),(81*3,24*3),(81*3,33*3),(69*3,40*3),(82*3,40*3)],fill=ink,width=3*3)
  svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><circle cx="49" cy="49" r="43" fill="none" stroke="'+ink+'" stroke-width="4"/><path fill="'+ink+'" d="M34 17H48L72 75 59 81 45 45 34 77H19L38 31Z"/><path d="M69 24H81V33L69 40H82" fill="none" stroke="'+ink+'" stroke-width="3"/></svg>'
  target=design/'half-life.webp'
 image=image.resize((96,96),Image.Resampling.LANCZOS);image.save(target,format='WEBP',lossless=True,method=6);(design/(name+'.svg')).write_text(svg)
 print(name,target.stat().st_size,'bytes')
print('Focus type:',len(packed),'GPU bytes;',len(zlib.compress(packed,9)),'compressed')
