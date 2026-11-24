#!/usr/bin/env python3
"""Repack existing menu resources, replacing only the licensed catalog font."""
import argparse,hashlib,json,os,struct,subprocess,tempfile
from pathlib import Path

def unpack(data):
    h=struct.unpack_from('<10Q',data);assert h[0]==80
    dm,ds,fm,fs,files=h[3],h[4],h[7],h[8],h[9]
    assert dm+ds<=len(data) and fm+fs<=len(data)
    result={};seen=set()
    def name(at,n,end):
        assert n<256 and at+n<=end
        s=data[at:at+n].decode();assert s not in ('.','..') and not any(c in s for c in '/\\\x00');return s
    def visit(index,parent):
        assert index not in seen and index+24<=ds;seen.add(index)
        p,sibling,child,file,nxt,n=struct.unpack_from('<6I',data,dm+index)
        part=name(dm+index+24,n,dm+ds);path=parent/part
        visited=set()
        while file!=0xffffffff:
            assert file not in visited and file+32<=fs;visited.add(file)
            p,sibling_file,offset,size,nxt,n=struct.unpack_from('<IIQQII',data,fm+file)
            file_name=name(fm+file+32,n,fm+fs);key=(path/file_name).as_posix()
            assert key not in result and files+offset+size<=len(data)
            result[key]=data[files+offset:files+offset+size];file=sibling_file
        while child!=0xffffffff:
            next_child=struct.unpack_from('<I',data,dm+child+4)[0];visit(child,path);child=next_child
    visit(0,Path());return result

def build(base,output,tool):
    original=unpack((base/'engines/iz2s-core/runtime/package/iz2s/izlauncher.romfs').read_bytes())
    assert 'menu/catalog-font.ttf' in original
    font=base/'language/fonts/DejaVuSans-Bold.ttf';replacement=font.read_bytes()
    with tempfile.TemporaryDirectory(prefix='iz2s-font-romfs-') as tmp:
        root=Path(tmp)/'romfs';root.mkdir()
        for name,data in original.items():
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(replacement if name=='menu/catalog-font.ttf' else data)
        packed=Path(tmp)/'updated.romfs';subprocess.run([tool,root,packed],check=True,stdout=subprocess.DEVNULL)
        data=packed.read_bytes();actual=unpack(data);assert set(actual)==set(original)
        for name,before in original.items():assert actual[name]==(replacement if name=='menu/catalog-font.ttf' else before),name
        output.parent.mkdir(parents=True,exist_ok=True);staged=output.with_suffix('.romfs.tmp');staged.write_bytes(data);os.replace(staged,output)
    print(json.dumps({'files':len(original),'replaced':'menu/catalog-font.ttf','fontSha256':hashlib.sha256(replacement).hexdigest(),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('output',type=Path);p.add_argument('tool',type=Path);a=p.parse_args();build(a.base.resolve(),a.output.resolve(),a.tool.resolve())
