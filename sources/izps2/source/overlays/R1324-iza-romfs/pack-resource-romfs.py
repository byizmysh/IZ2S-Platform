"""Deterministically pack the explicit resource tree; reject symlinks/specials."""
from pathlib import Path
import hashlib,json,os,stat,struct,sys
W=Path(__file__).resolve().parents[1]
MASK=(1<<64)-1
def fnv(data):
 h=14695981039346656037
 for c in data:h=((h^c)*1099511628211)&MASK
 return h
def pack(root,output):
 root=Path(root);output=Path(output)
 assert not root.is_symlink() and root.is_dir()
 records=[('/resources',1,b'',None)]
 def walk(p):
  for e in sorted(os.scandir(p),key=lambda x:x.name.encode('utf8')):
   mode=e.stat(follow_symlinks=False).st_mode;path=Path(e.path)
   assert not stat.S_ISLNK(mode),('symlink rejected',str(path))
   name='/resources/'+path.relative_to(root).as_posix()
   raw=name.encode('utf8');assert len(raw)<256 and all(x not in ['','.','..'] for x in name.split('/')[1:])
   assert not any(ord(c)<32 or ord(c)==127 or c in '\\:' for c in name)
   if stat.S_ISDIR(mode):records.append((name,1,b'',None));walk(path)
   else:assert stat.S_ISREG(mode),('special file rejected',str(path));records.append((name,2,path.read_bytes(),str(path.resolve())))
 walk(root);records.sort(key=lambda x:x[0].encode('utf8'));assert len(records)<=256
 paths=bytearray();prepared=[]
 for name,kind,data,source in records:
  offset=len(paths);raw=name.encode()+b'\0';paths.extend(raw)
  prepared.append(dict(path=name,kind=kind,pathOffset=offset,pathBytes=len(raw),data=data,source=source,
                       bytes=len(data),fnv=fnv(data)if kind==2 else 0,sha256=hashlib.sha256(data).hexdigest()if kind==2 else None))
 path_offset=64+40*len(prepared);data_offset=(path_offset+len(paths)+7)&~7
 payload=bytearray();index=bytearray()
 for x in prepared:
  at=data_offset+len(payload)if x['kind']==2 else 0
  index.extend(struct.pack('<IIIIQQQ',x['pathOffset'],x['pathBytes'],x['kind'],0,at,x['bytes'],x['fnv']))
  if x['kind']==2:
   payload.extend(x['data']);payload.extend(b'\0'*((-len(payload))&7))
 prefix=index+paths+b'\0'*(data_offset-path_offset-len(paths))
 header=struct.pack('<8sIIQQIIQQQ',b'PS2ROMF1',1,64,data_offset+len(payload),64,len(prepared),40,path_offset,data_offset,fnv(prefix))
 blob=header+prefix+payload;output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(blob)
 manifest=dict(format='ps2-resource-romfs-pack/1',root=str(root.resolve()),blob=str(output.resolve()),
  blobBytes=len(blob),blobSha256=hashlib.sha256(blob).hexdigest(),entries=len(prepared),
  files=sum(x['kind']==2 for x in prepared),directories=sum(x['kind']==1 for x in prepared),
  resourceBytes=sum(x['bytes']for x in prepared),pathBytes=len(paths),indexBytes=len(index),
  symlinksAllowed=False,rootNamespace='/resources',deterministic=True,
  records=[{k:v for k,v in x.items()if k!='data'}for x in prepared])
 return manifest
if __name__ == '__main__':
 if len(sys.argv) != 3:
  raise SystemExit('Usage: python3 pack-resource-romfs.py resources-directory resources.romfs')
 report = pack(Path(sys.argv[1]), Path(sys.argv[2]))
 Path(sys.argv[2]).with_name('resource-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
