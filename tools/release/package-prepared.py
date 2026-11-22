#!/usr/bin/env python3
"""Verify and deterministically archive two prepared IZ2S installation trees."""
from pathlib import Path
import argparse,gzip,hashlib,json,stat,subprocess,tarfile,zipfile
EPOCH=1795305600  # 2026-11-22 00:00:00 UTC
STAMP=(2026,11,22,0,0,0)
def digest(file):
 with file.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--workspace',type=Path,required=True)
 args=parser.parse_args();workspace=args.workspace.resolve()
 out=workspace/'archives';out.mkdir(exist_ok=True)
 reports=workspace/'reports';reports.mkdir(exist_ok=True)
 stages=sorted(p for p in (workspace/'staging').iterdir() if p.is_dir())
 if len(stages)!=2:raise RuntimeError('Prepare exactly one Linux tree and one Windows x64 tree')
 results=[];seen=set()
 for stage in stages:
  profile=json.loads((stage/'distribution/profile.json').read_text());target=profile['releaseTarget']
  if target not in {'linux','windows-x64'} or target in seen:raise RuntimeError('Unexpected or duplicate release target')
  seen.add(target)
  if stage.name!='IZ2S-Platform-'+profile['version']+'-'+target:raise RuntimeError('Folder name does not match release profile')
  result=subprocess.run(['node','distribution/verify.cjs'],cwd=stage,text=True,capture_output=True)
  (reports/('check-'+target+'.txt')).write_text(result.stdout+result.stderr)
  if result.returncode:raise RuntimeError(result.stdout+result.stderr)
  inventory=json.loads((stage/'distribution/files.json').read_text())['files']
  paths=sorted(stage.rglob('*'));files=[p for p in paths if p.is_file()]
  if any(p.is_symlink() for p in paths):raise RuntimeError('Symlink in prepared tree')
  if {p.relative_to(stage).as_posix() for p in files}!=set(inventory)|{'distribution/files.json'}:raise RuntimeError('Unrecorded files in prepared tree')
  for directory in ['data','dist']:
   if not (stage/directory).is_dir() or any((stage/directory).iterdir()):raise RuntimeError('User state or old deployment in prepared tree')
  forbidden=[p for p in files if any(x in p.relative_to(stage).parts for x in ['.git','.versions','.venv','.local-setup','__pycache__','test-data']) or p.suffix.lower() in ['.core','.log','.key','.pem','.pyc'] or p.name.lower() in ['prod.keys','title.keys','.env']]
  if forbidden:raise RuntimeError('Private/generated files in prepared tree')
  expected={stage.name+'/'+p.relative_to(stage).as_posix():digest(p) for p in files}
  archive=out/(stage.name+('.tar.gz' if target=='linux' else '.zip'))
  if archive.exists():raise RuntimeError('Archive already exists; use a fresh workspace')
  if target=='linux':
   with archive.open('wb') as output,gzip.GzipFile(filename='',fileobj=output,mode='wb',compresslevel=6,mtime=EPOCH) as compressed,tarfile.open(fileobj=compressed,mode='w',format=tarfile.PAX_FORMAT) as tar:
    for p in [stage,*paths]:
     arc=stage.name+(('/'+p.relative_to(stage).as_posix()) if p!=stage else '')
     info=tar.gettarinfo(str(p),arcname=arc);info.uid=info.gid=0;info.uname=info.gname='';info.mtime=EPOCH
     info.mode=0o755 if p.is_dir() or p.stat().st_mode&0o111 else 0o644
     if p.is_file():
      with p.open('rb') as stream:tar.addfile(info,stream)
     else:tar.addfile(info)
   checked=set()
   with tarfile.open(archive,'r|gz') as tar:
    for member in tar:
     if not member.isfile():continue
     if member.name in checked or member.name not in expected:raise RuntimeError('Unexpected archive member')
     with tar.extractfile(member) as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
     if actual!=expected[member.name]:raise RuntimeError('Archive readback failed: '+member.name)
     checked.add(member.name)
  else:
   with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as z:
    for p in files:
     info=zipfile.ZipInfo(stage.name+'/'+p.relative_to(stage).as_posix(),STAMP);info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=(stat.S_IFREG|0o644)<<16
     with p.open('rb') as source,z.open(info,'w') as output:
      while block:=source.read(1024*1024):output.write(block)
    for name in ['data','dist']:
     info=zipfile.ZipInfo(stage.name+'/'+name+'/',STAMP);info.external_attr=((stat.S_IFDIR|0o755)<<16)|0x10;z.writestr(info,b'')
   checked=set()
   with zipfile.ZipFile(archive) as z:
    for info in z.infolist():
     if info.is_dir():continue
     if info.filename in checked or info.filename not in expected:raise RuntimeError('Unexpected archive member')
     with z.open(info) as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
     if actual!=expected[info.filename]:raise RuntimeError('Archive readback failed: '+info.filename)
     checked.add(info.filename)
  if checked!=set(expected):raise RuntimeError('Archive member missing')
  report={'archive':archive.name,'bytes':archive.stat().st_size,'sha256':digest(archive),'files':len(files),'unpackedBytes':sum(p.stat().st_size for p in files),'target':target,'archiveReadbackVerified':True,'check':'passed'}
  results.append(report);print(json.dumps(report),flush=True)
 (out/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['archive']+'\n' for r in results))
 (reports/'artifacts.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
