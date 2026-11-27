#!/usr/bin/env python3
"""Verify and deterministically archive the prepared Linux installation tree."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
from shutil import copyfileobj
from time import gmtime
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile, ZipInfo
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
    retired = ('engines/civswitch/', 'engines/civswitchldn/', 'engines/izcivswitchfs/', 'modules/vacootube/')
    if any(name.lower().startswith(retired) for name in inventory['files']):
        raise RuntimeError('Retired engine or module in prepared tree')
    for name in ['data','dist']:
        if not (stage/name).is_dir() or any((stage/name).iterdir()):raise RuntimeError('User state or old deployment in prepared tree')
    forbidden=[p for p in files if any(x in p.relative_to(stage).parts for x in ['.git','.versions','.venv','.local-setup','__pycache__','test-data']) or p.suffix.lower() in ['.core','.log','.key','.pem','.pyc','.exe','.dll','.cmd','.vbs'] or p.name.lower() in ['prod.keys','title.keys','.env']]
    if forbidden:raise RuntimeError('Private/generated files or Windows executables in Linux tree: '+str(forbidden[0].relative_to(stage)))
    sd=out/SD_NAME
    archive=out/(stage.name+'.zip')
    if sd.exists() or archive.exists():
        raise RuntimeError('Archive already exists; use a fresh workspace')
    subprocess.run(['node','--input-type=module','-e',
        "import {writeFile} from 'node:fs/promises';"
        "import {buildSdPackage} from './engines/iz2sciv-core/tools/sd-package.mjs';"
        "await writeFile(process.argv[1], (await buildSdPackage()).bytes, {flag:'wx'});",str(sd)],cwd=stage,check=True)
    sd_hash=digest(sd)
    inventory['files'][SD_NAME]={'bytes':sd.stat().st_size,'sha256':sd_hash,'executable':False}
    bundled_inventory=(json.dumps(inventory,ensure_ascii=False,indent=2)+'\n').encode()
    expected={stage.name+'/'+p.relative_to(stage).as_posix():digest(p) for p in files}
    expected[stage.name+'/'+inventory_path]=hashlib.sha256(bundled_inventory).hexdigest()
    expected[stage.name+'/'+SD_NAME]=sd_hash
    sd_member = stage.name + '/' + SD_NAME
    timestamp = gmtime(EPOCH)[:6]
    with ZipFile(archive, 'x', compression=ZIP_DEFLATED, compresslevel=6) as zipped:
        for file in [stage, *paths, sd]:
            directory = file.is_dir()
            name = sd_member if file == sd else (stage.name if file == stage else stage.name + '/' + file.relative_to(stage).as_posix())
            info = ZipInfo(name + ('/' if directory else ''), timestamp)
            info.create_system = 3
            if directory:
                info.external_attr = (0o40755 << 16) | 0x10
                zipped.writestr(info, b'')
                continue
            relative = name.split('/', 1)[1]
            executable = inventory['files'].get(relative, {}).get('executable', False)
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            info.compress_type = ZIP_STORED if file == sd else ZIP_DEFLATED
            if relative == inventory_path:
                zipped.writestr(info, bundled_inventory)
                continue
            info.file_size = file.stat().st_size
            with file.open('rb') as source, zipped.open(info, 'w') as dest:
                copyfileobj(source, dest, 1024 * 1024)
    checked = set()
    with ZipFile(archive) as zipped:
        for member in zipped.infolist():
            if member.is_dir():
                continue
            name = member.filename
            if name in checked or name not in expected:
                raise RuntimeError('Unexpected archive member: ' + name)
            with zipped.open(member) as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != expected[name]:
                    raise RuntimeError('Archive readback failed: ' + name)
            relative = name.split('/', 1)[1]
            executable = bool((member.external_attr >> 16) & 0o111)
            if executable != inventory['files'].get(relative, {}).get('executable', False):
                raise RuntimeError('Archive executable mode differs: ' + name)
            checked.add(name)
    if checked != set(expected):
        raise RuntimeError('Archive member missing')
    report = {'archive':archive.name,'bytes':archive.stat().st_size,'sha256':digest(archive),
              'target':'linux','files':len(expected),'archiveReadbackVerified':True,
              'sd':{'path':sd_member,'bytes':sd.stat().st_size,'sha256':sd_hash},'check':'passed'}
    (out/'SHA256SUMS.txt').write_text(
        f"{report['sha256']}  {archive.name}\n{sd_hash}  {sd.name}\n")
    (reports/'artifacts.json').write_text(json.dumps([report],indent=2)+'\n')
    print(json.dumps(report),flush=True)

if __name__=='__main__':main()
