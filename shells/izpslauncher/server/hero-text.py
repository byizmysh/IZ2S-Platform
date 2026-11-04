"""High-resolution 2-bit titles in spare catalog bytes; stable ABI fields stay intact."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json,os,struct,sys
apps=json.loads(Path(sys.argv[1]).read_text());slots=bytearray(Path(sys.argv[2]).read_bytes());font=str(Path(__file__).resolve().parents[3]/'language/fonts/DejaVuSans-Bold.ttf')
for i,app in enumerate(apps):
 text=app['name'];size=122
 if any('\u3400'<=ch<='\u9fff' for ch in text) and Path('/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc').is_file():font='/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc'
 while size>26:
  f=ImageFont.truetype(font,size);b=f.getbbox(text)
  if b[2]-b[0]<=508 and b[3]-b[1]<=94:break
  size-=2
 while f.getbbox(text)[2]>508:text=text[:-2]+'…'
 im=Image.new('L',(512,96));d=ImageDraw.Draw(im);b=d.textbbox((0,0),text,font=f);w,h=b[2]-b[0],b[3]-b[1];d.text((-b[0],-b[1]),text,font=f,fill=255);pixels=im.tobytes();packed=bytearray(len(pixels)//4)
 for n,v in enumerate(pixels):packed[n//4]|=((v*3+127)//255)<<((n%4)*2)
 base=i*131072;name=app.get('searchName',app['name']).lower().encode('ascii','ignore')[:63];slots[base+102432:base+102496]=name.ljust(64,b'\0');struct.pack_into('<2I',slots,base+102420,w,h);slots[base+106496:base+118784]=packed
Path(sys.argv[2]).write_bytes(slots)
