#!/usr/bin/env python3
"""Verify and deterministically archive the prepared Linux installation tree."""
from pathlib import Path
import argparse,gzip,hashlib,json,subprocess,tarfile
EPOCH=1795564800  # 2026-11-25 00:00:00 UTC

def digest(file):
    with file.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--workspace',type=Path,required=True)
    workspace=parser.parse_args().workspace.resolve()
    out=workspace/'archives';out.mkdir(exist_ok=True)
    reports=workspace/'reports';reports.mkdir(exist_ok=True)
    stages=sorted(p for p in (workspace/'staging').iterdir() if p.is_dir())
    if len(stages)!=1:raise RuntimeError('Prepare exactly one Linux installation tree')
    stage=stages[0];profile=json.loads((stage/'distribution/profile.json').read_text())
    if profile['releaseTarget']!='linux':raise RuntimeError('This edition packages Linux only')
    if stage.name!='IZ2S-'+profile['version']+'-linux':raise RuntimeError('Folder name does not match release profile')
    result=subprocess.run(['node','distribution/verify.cjs'],cwd=stage,text=True,capture_output=True)
    (reports/'check-linux.txt').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    inventory=json.loads((stage/'distribution/files.json').read_text())['files']
    paths=sorted(stage.rglob('*'));files=[p for p in paths if p.is_file()]
    if any(p.is_symlink() for p in paths):raise RuntimeError('Symlink in prepared tree')
    if {p.relative_to(stage).as_posix() for p in files}!=set(inventory)|{'distribution/files.json'}:raise RuntimeError('Unrecorded files in prepared tree')
    for name in ['data','dist']:
        if not (stage/name).is_dir() or any((stage/name).iterdir()):raise RuntimeError('User state or old deployment in prepared tree')
    forbidden=[p for p in files if any(x in p.relative_to(stage).parts for x in ['.git','.versions','.venv','.local-setup','__pycache__','test-data']) or p.suffix.lower() in ['.core','.log','.key','.pem','.pyc','.exe','.dll','.cmd','.vbs'] or p.name.lower() in ['prod.keys','title.keys','.env']]
    if forbidden:raise RuntimeError('Private/generated files or Windows executables in Linux tree: '+str(forbidden[0].relative_to(stage)))
    expected={stage.name+'/'+p.relative_to(stage).as_posix():digest(p) for p in files}
    archive=out/('IZ2S-'+profile['version']+'-linux.tar.gz')
    if archive.exists():raise RuntimeError('Archive already exists; use a fresh workspace')
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
    if checked!=set(expected):raise RuntimeError('Archive member missing')
    report={'archive':archive.name,'bytes':archive.stat().st_size,'sha256':digest(archive),'files':len(files),'unpackedBytes':sum(p.stat().st_size for p in files),'target':'linux','archiveReadbackVerified':True,'check':'passed'}
    (out/'SHA256SUMS.txt').write_text(report['sha256']+'  '+report['archive']+'\n')
    (reports/'artifacts.json').write_text(json.dumps([report],indent=2)+'\n')
    print(json.dumps(report),flush=True)

if __name__=='__main__':main()
