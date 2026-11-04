"""Rasterize a bounded, English activity feed into its own 2-bit atlas mailbox."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from datetime import datetime
import json, struct, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from language.fonts import font
def t(text): return str(text)

source=json.loads(Path(sys.argv[1]).read_text())
events=source['events'][:6]
apps=source['apps']
image=Image.new('L',(1024,512))
draw=ImageDraw.Draw(image)
title=ImageFont.truetype(font(),20)
detail=ImageFont.truetype(font(),15)
date=ImageFont.truetype(font(),14)
header=bytearray(128)
struct.pack_into('<II',header,0,0x50534143,len(events))

def fit(text,font,width):
    text=' '.join(str(text).split())
    if draw.textlength(text,font=font)<=width:return text
    while text and draw.textlength(text+'…',font=font)>width:text=text[:-1]
    return text.rstrip()+'…'

def label(text,font,x,y):
    bbox=draw.textbbox((0,0),text,font=font)
    draw.text((x-bbox[0],y-bbox[1]),text,font=font,fill=255)

months=('Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec')
for i,event in enumerate(events):
    index=next((n for n,a in enumerate(apps) if a['id']==event['packageId']),0xffffffff)
    kind={'installed':1,'updated':2,'removed':4}.get(event['type'],3)
    struct.pack_into('<II',header,16+i*16,index,kind)
    name=t(apps[index]['name'] if index!=0xffffffff else event.get('name','Module'))
    version=event.get('version','')
    action=t({1:'Installed',2:'Updated',3:'Added to menu',4:'Removed from console'}[kind])
    if version and kind!=4:action+=' · v'+version
    at=datetime.fromisoformat(event['at'].replace('Z','+00:00')).astimezone()
    when=f'{at.day} {months[at.month-1]} {at.year} · {at:%H:%M}'
    y=i*64
    label(fit(name,title,492),title,0,y)
    label(fit(action,detail,492),detail,0,y+31)
    width=draw.textbbox((0,0),when,font=date)[2]
    label(when,date,788-width,y+4)

pixels=image.tobytes()
packed=bytearray(len(pixels)//4)
for n,value in enumerate(pixels):packed[n//4]|=((value*3+127)//255)<<((n%4)*2)
Path(sys.argv[2]).write_bytes(header+packed)
