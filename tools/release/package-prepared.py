#!/usr/bin/env python3
"""Verify and deterministically archive the prepared Linux installation tree."""
from pathlib import Path
import argparse,gzip,hashlib,json,subprocess,tarfile
from io import BytesIO
from shutil import copyfileobj
from time import gmtime
from zipfile import ZIP_STORED, ZipFile, ZipInfo
EPOCH=1795564800  # 2026-11-25 00:00:00 UTC
SD_NAME='iz2sciv-core-sd.zip'

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
    inventory_path='distribution/files.json'
    inventory=json.loads((stage/inventory_path).read_text())
    paths=sorted(stage.rglob('*'));files=[p for p in paths if p.is_file()]
    if any(p.is_symlink() for p in paths):raise RuntimeError('Symlink in prepared tree')
    if {p.relative_to(stage).as_posix() for p in files}!=set(inventory['files'])|{inventory_path}:raise RuntimeError('Unrecorded files in prepared tree')
    if SD_NAME in inventory['files']:raise RuntimeError('Use a prepared tree without a prebuilt SD archive')
    for name in ['data','dist']:
        if not (stage/name).is_dir() or any((stage/name).iterdir()):raise RuntimeError('User state or old deployment in prepared tree')
    forbidden=[p for p in files if any(x in p.relative_to(stage).parts for x in ['.git','.versions','.venv','.local-setup','__pycache__','test-data']) or p.suffix.lower() in ['.core','.log','.key','.pem','.pyc','.exe','.dll','.cmd','.vbs'] or p.name.lower() in ['prod.keys','title.keys','.env']]
    if forbidden:raise RuntimeError('Private/generated files or Windows executables in Linux tree: '+str(forbidden[0].relative_to(stage)))
    sd=out/SD_NAME
    archive=out/(stage.name+'.tar.gz')
    outer=out/(stage.name+'.zip')
    if any(p.exists() for p in (sd,archive,outer)):raise RuntimeError('Archive already exists; use a fresh workspace')
    subprocess.run(['node','--input-type=module','-e',
        "import {writeFile} from 'node:fs/promises';"
        "import {buildSdPackage} from './engines/IzCivSwitchFS/tools/sd-package.mjs';"
        "await writeFile(process.argv[1], (await buildSdPackage()).bytes, {flag:'wx'});",str(sd)],cwd=stage,check=True)
    sd_hash=digest(sd)
    inventory['files'][SD_NAME]={'bytes':sd.stat().st_size,'sha256':sd_hash,'executable':False}
    bundled_inventory=(json.dumps(inventory,ensure_ascii=False,indent=2)+'\n').encode()
    expected={stage.name+'/'+p.relative_to(stage).as_posix():digest(p) for p in files}
    expected[stage.name+'/'+inventory_path]=hashlib.sha256(bundled_inventory).hexdigest()
    expected[stage.name+'/'+SD_NAME]=sd_hash
    sd_member=stage.name+'/'+SD_NAME
    with archive.open('wb') as output,gzip.GzipFile(filename='',fileobj=output,mode='wb',compresslevel=6,mtime=EPOCH) as compressed,tarfile.open(fileobj=compressed,mode='w',format=tarfile.PAX_FORMAT) as tar:
        for p in [stage,*paths]:
            arc=stage.name+(('/'+p.relative_to(stage).as_posix()) if p!=stage else '')
            info=tar.gettarinfo(str(p),arcname=arc);info.uid=info.gid=0;info.uname=info.gname='';info.mtime=EPOCH
            info.mode=0o755 if p.is_dir() or p.stat().st_mode&0o111 else 0o644
            if p==stage/inventory_path:
                info.size=len(bundled_inventory)
                tar.addfile(info,BytesIO(bundled_inventory))
            elif p.is_file():
                with p.open('rb') as stream:tar.addfile(info,stream)
            else:tar.addfile(info)
        info=tarfile.TarInfo(sd_member);info.size=sd.stat().st_size;info.mtime=EPOCH;info.mode=0o644
        with sd.open('rb') as stream:tar.addfile(info,stream)
    checked=set()
    with tarfile.open(archive,'r|gz') as tar:
        for member in tar:
            if not member.isfile():continue
            if member.name in checked or member.name not in expected:raise RuntimeError('Unexpected archive member')
            with tar.extractfile(member) as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
            if actual!=expected[member.name]:raise RuntimeError('Archive readback failed: '+member.name)
            checked.add(member.name)
    if checked!=set(expected):raise RuntimeError('Archive member missing')
    archive_hash=digest(archive)
    info=ZipInfo(archive.name,gmtime(EPOCH)[:6]);info.create_system=3;info.external_attr=0o100644 << 16
    with ZipFile(outer,'x',compression=ZIP_STORED) as zipped:
        with archive.open('rb') as source,zipped.open(info,'w') as dest:copyfileobj(source,dest,1024*1024)
    with ZipFile(outer) as zipped:
        if zipped.namelist()!=[archive.name]:raise RuntimeError('Unexpected outer ZIP inventory')
        with zipped.open(archive.name) as stream:
            if hashlib.file_digest(stream,'sha256').hexdigest()!=archive_hash:raise RuntimeError('Outer ZIP readback failed')
    report={'archive':outer.name,'bytes':outer.stat().st_size,'sha256':digest(outer),'target':'linux','archiveReadbackVerified':True,
            'tar':{'path':archive.name,'bytes':archive.stat().st_size,'sha256':archive_hash,'files':len(expected)},
            'sd':{'path':sd_member,'bytes':sd.stat().st_size,'sha256':sd_hash},'check':'passed'}
    (out/'SHA256SUMS.txt').write_text(f"{report['sha256']}  {outer.name}\n{archive_hash}  {archive.name}\n{sd_hash}  {sd.name}\n")
    (reports/'artifacts.json').write_text(json.dumps([report],indent=2)+'\n')
    print(json.dumps(report),flush=True)

if __name__=='__main__':main()
