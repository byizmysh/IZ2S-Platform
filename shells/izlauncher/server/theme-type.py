"""Update the production brand glyphs; preserve atlas dimensions and offsets."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import re,zlib
root=Path(__file__).resolve().parents[3]
app=Path(__file__).resolve().parent.parent/'app'
header=(app/'shaders/focus-type.glsl').read_text()
x,y,w,h=map(int,re.search(r'F_TITLE=ivec4\((\d+),(\d+),(\d+),(\d+)\)',header).groups())
im=Image.open(app/'assets/focus-type.png').convert('L');d=ImageDraw.Draw(im)
d.rectangle((x,y,x+w-1,y+h-1),fill=0)
font=str(root/'language/fonts/DejaVuSans-Bold.ttf')
size=76
while True:
 f=ImageFont.truetype(font,size);bounds=d.textbbox((0,0),'IZlauncher',font=f)
 if bounds[2]-bounds[0]<=w and bounds[3]-bounds[1]<=h:break
 size-=1
d.text((x-bounds[0],y-bounds[1]),'IZlauncher',font=f,fill=255)
im.save(app/'assets/focus-type.png')
packed=bytearray(zlib.decompress((app/'assets/focus-type.deflate').read_bytes()))
for yy in range(y,y+h):
 for xx in range(x,x+w):
  pixel=yy*im.width+xx;shift=(pixel%4)*2
  packed[pixel//4]=(packed[pixel//4]&~(3<<shift))|(((im.getpixel((xx,yy))*3+127)//255)<<shift)
(app/'assets/focus-type.deflate').write_bytes(zlib.compress(packed,9))
print('IZlauncher brand updated in the copied atlas; GPU atlas bytes unchanged:',len(packed))

# The qualification renderer shares legacy glyph coordinates with applications.
# Paint its brand inside the original rectangle as well.
server=Path(__file__).resolve().parent/'shaders'
legacy_header=(server/'launcher-text.glsl').read_text()
x,y,w,h=map(int,re.search(r'TEXT_TITLE = ivec4\((\d+),(\d+),(\d+),(\d+)\)',legacy_header).groups())
im=Image.open(server/'launcher-text.png').convert('L');d=ImageDraw.Draw(im);d.rectangle((x,y,x+w-1,y+h-1),fill=0)
size=48
while True:
 f=ImageFont.truetype(font,size);bounds=d.textbbox((0,0),'IZlauncher 0.22',font=f)
 if bounds[2]-bounds[0]<=w and bounds[3]-bounds[1]<=h:break
 size-=1
d.text((x-bounds[0],y-bounds[1]),'IZlauncher 0.22',font=f,fill=255)
im.save(server/'launcher-text.png')
raw=im.tobytes();packed=bytearray(len(raw)//4)
for i,v in enumerate(raw):packed[i//4]|=((v*3+127)//255)<<((i%4)*2)
(server/'launcher-text.bin').write_bytes(packed)
(server/'launcher-text.deflate').write_bytes(zlib.compress(packed,9))

# Additional theme labels fit inside the existing unused typography allocation.
import json
image_path=app/'assets/focus-type.png'
header_path=app/'shaders/focus-type.glsl'
header=header_path.read_text().split('// IZ2 theme labels')[0].rstrip()+'\n'
base_height=(int(re.search(r'F_ASCII=ivec4\(0,(\d+),',header).group(1))+99)//4*4
base_image=Image.open(image_path).convert('L').crop((0,0,768,base_height))
im=Image.new('L',(768,base_height+220));im.paste(base_image,(0,0));d=ImageDraw.Draw(im);y=base_height+4
extra=['// IZ2 theme labels: existing type allocation, no extra texture.']
for name,label,size in [('RECENT','Recent activity',26),('NOACTIVITY','No completed tasks yet',22),('SCROLL','Scroll to see more',18),('UPDOWN','Scroll',18),('SELECTED','Selected application',17)]:
 f=ImageFont.truetype(str(root/'language/fonts/DejaVuSans.ttf'),size);b=d.textbbox((0,0),label,font=f);w,h=b[2]-b[0],b[3]-b[1];d.text((2-b[0],y-b[1]),label,font=f,fill=255);extra.append(f'const ivec4 F_{name}=ivec4(2,{y},{w},{h});');y+=h+7
im=im.crop((0,0,768,(y+3)//4*4));im.save(image_path);raw=im.tobytes();packed=bytearray(len(raw)//4)
for i,v in enumerate(raw):packed[i//4]|=((v*3+127)//255)<<((i%4)*2)
combined=bytearray(zlib.decompress((app/'assets/focus-type.deflate').read_bytes()));assert len(packed)<=0x60000-65536
combined[:len(packed)]=packed
(app/'assets/focus-type.deflate').write_bytes(zlib.compress(combined,9));header_path.write_text(header+'\n'+'\n'.join(extra)+'\n')
metadata=json.loads((app/'assets/focus-type.json').read_text());metadata['height']=im.height;(app/'assets/focus-type.json').write_text(json.dumps(metadata)+'\n')
