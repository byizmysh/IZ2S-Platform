#!/usr/bin/env python3
"""Build local IZPSX assets. No uploads, repository creation or console actions."""
from pathlib import Path
import hashlib,json,os,subprocess,zipfile,tarfile,gzip,io
ROOT=Path(__file__).resolve().parents[1]
VERSION=json.loads((ROOT/'package.json').read_text())['version']
OUT=ROOT/'dist';EPOCH=1794268800
TOP={'src','shaders','server','tests','assets','vendor','abi','module','tools','.github'}
FILES={'README.md','BUILDING.md','FAQ.md','COMPATIBILITY.md','CHANGELOG.md','THIRD_PARTY.md','CONTRIBUTING.md','LICENSE','FONT-LICENSE.txt','package.json','requirements-build.txt','.gitignore','launch-options.cjs'}
def sha(data):return hashlib.sha256(data).hexdigest()
def files():
    result=[]
    for p in sorted(ROOT.rglob('*')):
        rel=p.relative_to(ROOT)
        if not p.is_file():continue
        if any(x in {'.git','.sdk','__pycache__','node_modules','build','dist','packages','data','user-games','user-bios'} for x in rel.parts):continue
        if len(rel.parts)==1 and str(rel) not in FILES:continue
        if len(rel.parts)>1 and rel.parts[0] not in TOP:continue
        if p.is_symlink():raise RuntimeError('Source link not allowed: '+str(rel))
        data=p.read_bytes()
        if b'/home/'+b'k2/' in data:raise RuntimeError('Workstation path in '+str(rel))
        if rel.suffix in {'.core','.log','.pyc','.key','.pem'}:raise RuntimeError('Private output in source: '+str(rel))
        result.append((rel.as_posix(),data,bool(p.stat().st_mode&0o111)))
    return result
def zipped(path,entries,prefix=''):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data,executable in entries:
            entry=zipfile.ZipInfo(prefix+name,(2026,11,10,0,0,0));entry.external_attr=(0o100755 if executable else 0o100644)<<16;entry.compress_type=zipfile.ZIP_DEFLATED;z.writestr(entry,data)
def main():
    OUT.mkdir(exist_ok=True)
    subprocess.run([os.environ.get('NODE','node'),'tools/package.cjs'],cwd=ROOT,check=True)
    source=files();manifest={'version':VERSION,'files':{name:{'sha256':sha(data),'bytes':len(data),'executable':executable} for name,data,executable in source}}
    manifest_bytes=(json.dumps(manifest,indent=2)+'\n').encode();(OUT/'SOURCE-MANIFEST.json').write_bytes(manifest_bytes)
    source.append(('SOURCE-MANIFEST.json',manifest_bytes,False))
    stem='izpsx-emulator-'+VERSION
    zipped(OUT/(stem+'-source.zip'),source,'izpsx-emulator/')
    with (OUT/(stem+'-source.tar.gz')).open('wb') as raw,gzip.GzipFile(fileobj=raw,mode='wb',filename='',mtime=0,compresslevel=9) as gz,tarfile.open(fileobj=gz,mode='w') as tar:
        for name,data,executable in source:
            info=tarfile.TarInfo('izpsx-emulator/'+name);info.size=len(data);info.mode=0o755 if executable else 0o644;info.mtime=EPOCH;tar.addfile(info,io.BytesIO(data))
    sd=[]
    for p in sorted((OUT/'sd').iterdir()):
        if p.is_file() and (p.name=='launcher.izapp' or p.suffix=='.izdata'):sd.append(('IZ2S/Homebrew/Modules/izpsx/'+p.name,p.read_bytes(),False))
    if not any(n.endswith('launcher.izapp') for n,_,_ in sd):raise RuntimeError('Native SD entry missing')
    zipped(OUT/(stem+'-sd.zip'),sd)
    products=[OUT/(stem+s) for s in ['.izm','-sd.zip','-source.zip','-source.tar.gz']]
    hashes=[{'file':p.name,'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())} for p in products]
    (OUT/'SHA256SUMS').write_text(''.join(p['sha256']+'  '+p['file']+'\n' for p in hashes))
    report={'version':VERSION,'published':False,'consoleExecuted':False,'nativeRebuilt':False,'correspondingSourceIncluded':True,'sourceFiles':len(source),'products':hashes}
    (OUT/'release-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
