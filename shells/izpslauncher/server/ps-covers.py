"""Compose portrait cards from each package's existing icon, without network assets."""
from PIL import Image,ImageOps,ImageDraw,ImageFont,ImageFilter
from pathlib import Path
import json,sys,struct
from ps_fonts import font as ps_font
apps=json.loads(Path(sys.argv[1]).read_text());slots=bytearray(Path(sys.argv[2]).read_bytes())
font=ps_font('DejaVuSans-Bold.ttf')
colors=[(29,107,158),(169,69,23),(136,28,33),(40,58,119),(47,106,110),(106,46,132)]
backgrounds={}
for i,app in enumerate(apps):
 w,h=256,384;accent=colors[i%len(colors)]
 if accent not in backgrounds:
  background=Image.new('RGB',(w,h));px=background.load()
  for y in range(h):
   for x in range(w):
    a=max(0,1-((x-100)**2+(y-95)**2)**.5/380)
    px[x,y]=tuple(int(8+c*a*.72) for c in accent)
  d=ImageDraw.Draw(background)
  for n in range(5):d.line([(w+100-n*38,-20),(-80+n*8,h)],fill=tuple(int(c*.38) for c in accent),width=1)
  backgrounds[accent]=background
 im=backgrounds[accent].copy()
 icon=Image.open(app['icon']).convert('RGBA');icon=ImageOps.contain(icon,(242,242),Image.Resampling.LANCZOS)
 # Icons retain their real artwork and are set on a full portrait cover.
 im.paste(icon,((w-icon.width)//2,36+(242-icon.height)//2),icon)
 im=im.resize((128,192),Image.Resampling.LANCZOS);data=bytearray()
 for r,g,b in zip(*[iter(im.tobytes())]*3):data+=struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3))
 base=i*131072;slots[base:base+len(data)]=data;struct.pack_into('<I',slots,base+102496,0x50534356)
 # A separate fixed-size title keeps collection rows on identical baselines.
 title=Image.new('L',(512,64));td=ImageDraw.Draw(title);face=ImageFont.truetype(ps_font(),30);name=app['name']
 while td.textlength(name,font=face)>508:name=name[:-2]+'…'
 bounds=td.textbbox((0,0),name,font=face);td.text((-bounds[0],-bounds[1]),name,font=face,fill=255)
 packed=bytearray(8192)
 for n,value in enumerate(title.tobytes()):packed[n//4]|=((value*3+127)//255)<<((n%4)*2)
 slots[base+122880:base+131072]=packed
 struct.pack_into('<2I',slots,base+102500,bounds[2]-bounds[0],bounds[3]-bounds[1])
 # Two-line portrait caption in a separate 2-bit atlas; never baked into cover pixels.
 caption=Image.new('L',(256,64));cd=ImageDraw.Draw(caption);cf=ImageFont.truetype(ps_font(),26)
 lines=['']
 for word in app['name'].split():
  trial=(lines[-1]+' '+word).strip()
  if cd.textlength(trial,font=cf)<=220:lines[-1]=trial
  elif len(lines)<2:lines.append(word)
  else:lines[-1]+=' '+word
 for row,line in enumerate(lines):
  while cd.textlength(line,font=cf)>220:line=line[:-2]+'…'
  bounds=cd.textbbox((0,0),line,font=cf);cd.text((-bounds[0],row*30-bounds[1]),line,font=cf,fill=255)
 cp=bytearray(4096)
 for n,value in enumerate(caption.tobytes()):cp[n//4]|=((value*3+127)//255)<<((n%4)*2)
 slots[base+118784:base+122880]=cp
Path(sys.argv[2]).write_bytes(slots)
