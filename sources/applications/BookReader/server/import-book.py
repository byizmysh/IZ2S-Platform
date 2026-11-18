#!/usr/bin/env python3
"""Bounded, text-only TXT/EPUB import. No network, scripts or external entities."""
import sys,json,zipfile,posixpath,urllib.parse
from pathlib import Path
from html.parser import HTMLParser
from xml.etree import ElementTree as ET
MAX=16*1024*1024
class Text(HTMLParser):
 def __init__(self):super().__init__(convert_charrefs=True);self.parts=[];self.skip=0
 def handle_starttag(self,tag,attrs):
  if tag in ('script','style','head'):self.skip+=1
  if not self.skip and tag in ('p','div','h1','h2','h3','h4','br','li','blockquote'):self.parts.append('\n\n' if tag!='br' else '\n')
 def handle_endtag(self,tag):
  if tag in ('script','style','head') and self.skip:self.skip-=1
  if not self.skip and tag in ('p','div','h1','h2','h3','li','blockquote'):self.parts.append('\n\n')
 def handle_data(self,data):
  if not self.skip:self.parts.append(data)
def xml(data):
 if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():raise ValueError('XML declarations with entities are unsupported')
 return ET.fromstring(data)
def read(path):
 suffix=path.suffix.lower()
 if suffix=='.txt':
  raw=path.read_bytes()
  if len(raw)>8*1024*1024:raise ValueError('TXT exceeds 8 MiB')
  try:text=raw.decode('utf-8-sig')
  except UnicodeDecodeError:text=raw.decode('cp1252')
  return {'title':path.stem,'text':text}
 if suffix!='.epub':raise ValueError('Choose an English TXT or DRM-free EPUB book')
 with zipfile.ZipFile(path) as archive:
  entries=archive.infolist()
  if len(entries)>4096 or sum(e.file_size for e in entries)>MAX:raise ValueError('EPUB expanded contents exceed the import limit')
  for e in entries:
   if e.filename.startswith('/') or '\\' in e.filename or '..' in e.filename.split('/') or e.flag_bits&1:raise ValueError('Unsafe or encrypted EPUB entry')
  def get(name):
   if name.startswith('/') or '..' in name.split('/'):raise ValueError('EPUB path leaves the archive')
   e=archive.getinfo(name)
   if e.file_size>8*1024*1024:raise ValueError('EPUB entry is too large')
   return archive.read(e)
  if 'META-INF/encryption.xml' in archive.namelist():raise ValueError('Encrypted EPUB books are unsupported')
  container=xml(get('META-INF/container.xml'));root=next((e.attrib.get('full-path') for e in container.iter() if e.tag.endswith('rootfile')),None)
  if not root:raise ValueError('EPUB has no package document')
  opf=xml(get(root));manifest={e.attrib['id']:e.attrib for e in opf.iter() if e.tag.endswith('}item')};title=next((e.text for e in opf.iter() if e.tag.endswith('}title') and e.text),path.stem);parts=[]
  for item in opf.iter():
   if not item.tag.endswith('}itemref'):continue
   info=manifest.get(item.attrib.get('idref'),{})
   if info.get('media-type') not in ('application/xhtml+xml','text/html'):continue
   href=urllib.parse.unquote(info.get('href','').split('#')[0]);name=posixpath.normpath(posixpath.join(posixpath.dirname(root),href));parser=Text();parser.feed(get(name).decode('utf-8-sig'));parts.append(''.join(parser.parts))
  if not parts:raise ValueError('EPUB has no readable text chapters')
  return {'title':title,'text':'\n\n'.join(parts)}
try:print(json.dumps(read(Path(sys.argv[1])),ensure_ascii=True))
except Exception as e:print(str(e),file=sys.stderr);sys.exit(1)
