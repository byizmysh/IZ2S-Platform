from pathlib import Path
from PIL import Image, ImageFont, ImageDraw
import sys
sys.path.insert(0,str(__import__("pathlib").Path(__file__).resolve().parents[3]))
from language.fonts import font as resolve_font
import struct

here=Path(__file__).resolve().parent
labels=['File Exchange','LIVE / SWITCH 2','Console folders','FTP TRANSFER','save + tmp: read / write','rom: read only','Games: OunceCake needs held FS RPC','A Open   B Back   X Refresh','L / R Page   Y FTP on/off   - Launcher','Starting service...','FTP ready','FTP stopped','Transfer in progress','Anonymous login / passive mode','One connection at a time','FILES','SENT KiB','RECEIVED KiB','Y Start / Stop FTP']
meta=[];strings=b''
for value in labels:
    data=value.encode('ascii');meta.extend([len(strings),len(data)]);strings+=data
font=Image.new('L',(128,84));draw=ImageDraw.Draw(font)
face=ImageFont.truetype(resolve_font('DejaVuSansMono.ttf'),12)
for ch in range(32,127):draw.text((((ch-32)%16)*8,((ch-32)//16)*14-1),chr(ch),font=face,fill=255)
packed=bytearray(128*84//4)
for i,v in enumerate(font.getdata()):packed[i//4]|=round(v*3/255)<<((i%4)*2)
header=[len(labels),16,(16+len(meta))*4,0]+[0]*12
header[3]=(header[2]+len(strings)+3)//4
data=struct.pack('<%dI'%(len(header)+len(meta)),*(header+meta))+strings
data+=bytes((-len(data))%4)+packed
(here/'assets/data.bin').write_bytes(data)
(here/'assets/state.bin').write_bytes(bytes(1024))

