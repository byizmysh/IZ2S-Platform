#!/usr/bin/env python3
"""Create an allowlisted IZ2S source/runtime release without private state."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,tempfile,os,re,zipfile,tarfile,gzip
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[1]
PROFILE=json.loads((ROOT/'distribution/profile.json').read_text())
TOP={'core','panel','engines','shells','hardware','profiles','sdk','tools','language','distribution','.github','docs'}
DOC_IMAGES={'docs/images/'+name+'.png' for name in ('connection','app-library','izlauncher','izpslauncher')}
ROOT_FILES={'start.cjs','start.sh','install.sh','install-desktop.sh','Install.cmd','Start.cmd','Install.command','Start.command','Install-Linux.desktop','package.json','LICENSE','COPYRIGHT','LICENSE-SCOPE.md','THIRD_PARTY.json','README.md','FAQ.md','COMPATIBILITY.md','RELEASE-NOTES.md','SUPPORT.md','.gitignore','preview.sh'}
PRIVATE={'.git','node_modules','__pycache__','.versions','data','runs','downloads','test-data','tests'}
PRIVATE_NAMES={'secret','capsule.key','installed.json','.engine-install.json'}
CREDENTIAL=re.compile(rb'-----BEGIN (?:OPENSSH |RSA |EC |DSA )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}')
EPOCH=int(datetime(2026,11,4,tzinfo=timezone.utc).timestamp())

def allowed(rel):
    parts=rel.parts
    if rel.as_posix()=='packages/iz2s-arm-input.tar.gz':return True
    if len(parts)==1:return rel.as_posix() in ROOT_FILES
    if any(p in PRIVATE or p.startswith(('.download-','.publish-','.placeholder-')) for p in parts):return False
    if rel.name in PRIVATE_NAMES or rel.suffix in {'.log','.pyc','.tmp','.izm','.pem','.key'}:return False
    if parts[0]=='desktop':return rel.as_posix() in {'desktop/install.cjs','desktop/izlauncher.svg'}
    if parts[0]=='docs':return rel.as_posix() in DOC_IMAGES
    if parts[0]=='modules':
        if 'runtime' in parts[2:]:return False
        if rel.as_posix()=='modules/README.md':return True
        return len(parts)>2 and parts[1] in PROFILE['moduleDirectories'] and 'docs' not in parts[2:]
    if parts[0] not in TOP:return False
    if parts[:3]==('engines','kurumuz','upstream'):return rel.name=='README.md'
    if parts[:2]==('core','session') and rel.suffix in {'.jpg','.png','.webp'}:return False
    return True

def source_manifest():
    file=ROOT/'SOURCE-MANIFEST.json'
    if not file.exists():return None
    document=json.loads(file.read_text());manifest=document.get('files') if isinstance(document,dict) else None
    if not isinstance(manifest,dict):raise RuntimeError('Invalid source manifest')
    for name,entry in manifest.items():
        if not name or any(c in name for c in ('\\',':','\0')) or any(p in {'','.','..'} for p in name.split('/')) or not isinstance(entry,dict) or not isinstance(entry.get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',entry['sha256']) or ('executable' in entry and not isinstance(entry['executable'],bool)):
            raise RuntimeError('Invalid source manifest entry')
        if not allowed(Path(name)):continue
        source=ROOT/name
        if not source.is_file() or source.is_symlink():raise RuntimeError('Release source is incomplete: '+name)
        content=source.read_bytes()
        if hashlib.sha256(content).hexdigest()!=entry['sha256'] or ('bytes' in entry and entry['bytes']!=len(content)):raise RuntimeError('Source checksum mismatch: '+name)
    return manifest

def copy_sources(stage):
    if stage.resolve()==ROOT.resolve() or ROOT.resolve() in stage.resolve().parents:raise RuntimeError('Choose an installation folder outside the extracted source folder')
    manifest=source_manifest()
    for source in sorted(ROOT.rglob('*')):
        rel=source.relative_to(ROOT)
        if not allowed(rel):continue
        if source.is_symlink():raise RuntimeError('Unexpected source symlink: '+str(rel))
        if not source.is_file():continue
        content=source.read_bytes()
        if manifest is not None and (str(rel.as_posix()) not in manifest or hashlib.sha256(content).hexdigest()!=manifest[rel.as_posix()]['sha256']):raise RuntimeError('Source checksum mismatch: '+str(rel))
        if CREDENTIAL.search(content):raise RuntimeError('Credential-like content in '+str(rel))
        if (b'/home/'+b'k2/') in content:raise RuntimeError('Private workstation path in '+str(rel))
        dest=stage/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(content)
        executable=manifest[rel.as_posix()].get('executable',False) if manifest is not None else source.stat().st_mode&0o111
        dest.chmod(0o755 if executable else 0o644)
    folder=stage/'engines/kurumuz/upstream';folder.mkdir(parents=True,exist_ok=True)
    (folder/'README.md').write_text('Install this optional original engine explicitly from the panel, or copy its pinned complete source here.\nSee ../engine.json for repository and revision.\n')
    (stage/'modules').mkdir(exist_ok=True)

def write_manifest(stage):
    files={}
    for file in sorted(stage.rglob('*')):
        if not file.is_file() or file.name=='SOURCE-MANIFEST.json':continue
        rel=file.relative_to(stage).as_posix()
        files[rel]={'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'bytes':file.stat().st_size,'executable':bool(file.stat().st_mode&0o111)}
    (stage/'SOURCE-MANIFEST.json').write_text(json.dumps({'version':PROFILE['version'],'repository':PROFILE['repository'],'files':files},indent=2)+'\n')
    return files

def archives(stage,out):
    prefix='iz2s-platform'
    products=[]
    for suffix in ['-source.zip','-linux.tar.gz','-windows-x64-experimental.zip','-macos-experimental.zip']:
        target=out/('iz2s-platform-'+PROFILE['version']+suffix)
        if suffix.endswith('.zip'):
            with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
                for f in sorted(stage.rglob('*')):
                    if not f.is_file():continue
                    info=zipfile.ZipInfo(prefix+'/'+f.relative_to(stage).as_posix(),(2026,11,4,0,0,0))
                    info.external_attr=(0o100755 if f.stat().st_mode&0o111 else 0o100644)<<16
                    info.compress_type=zipfile.ZIP_DEFLATED;archive.writestr(info,f.read_bytes())
                if 'experimental' in suffix:
                    entry='Install.cmd' if 'windows' in suffix else 'Install.command'
                    host='Windows x64' if 'windows' in suffix else 'macOS Intel / Apple silicon'
                    info=zipfile.ZipInfo(prefix+'/START-HERE.txt',(2026,11,4,0,0,0));info.external_attr=0o100644<<16;info.compress_type=zipfile.ZIP_DEFLATED
                    archive.writestr(info,'IZ2S Platform '+PROFILE['version']+' - '+host+'\nEXPERIMENTAL installer/panel candidate\n\nExtract this entire folder and open '+entry+'.\nPrivate Node.js/Python/Pillow are downloaded automatically.\nRead README.md, FAQ.md and COMPATIBILITY.md before proceeding.\nNative console session descriptors remain Linux-only.\nActual OS installation and end-to-end qualification are pending.\nThe installer does not upload files or publish content.\n')
        else:
            with target.open('wb') as raw,gzip.GzipFile(fileobj=raw,mode='wb',filename='',mtime=0,compresslevel=9) as compressed,tarfile.open(fileobj=compressed,mode='w',format=tarfile.PAX_FORMAT) as archive:
                for f in sorted(stage.rglob('*')):
                    if not f.is_file():continue
                    info=archive.gettarinfo(str(f),arcname=prefix+'/'+f.relative_to(stage).as_posix());info.uid=info.gid=0;info.uname=info.gname='';info.mtime=EPOCH;info.mode=0o755 if f.stat().st_mode&0o111 else 0o644;info.pax_headers={}
                    with f.open('rb') as stream:archive.addfile(info,stream)
        products.append({'file':target.name,'bytes':target.stat().st_size,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    (out/'SHA256SUMS').write_text(''.join(p['sha256']+'  '+p['file']+'\n' for p in products))
    return products

def build(out,rebuild):
    out=out.resolve();out.mkdir(parents=True,exist_ok=True)
    node=os.environ.get('NODE','node')
    build_env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'}
    with tempfile.TemporaryDirectory(prefix='iz2s-release-') as temporary:
        stage=Path(temporary)/'iz2s-platform';stage.mkdir();copy_sources(stage)
        if rebuild:
            subprocess.run([node,'tools/build-launchers.cjs'],cwd=stage,env=build_env,check=True)
            subprocess.run([node,'distribution/build-modules.cjs'],cwd=stage,env=build_env,check=True)
        else:
            subprocess.run([node,'-e',"require('./sdk/publish-modules.cjs').publish(process.cwd()).then(r=>{if(r.errors.length)throw Error(JSON.stringify(r.errors))})"],cwd=stage,env=build_env,check=True)
        # A builder cache or module data directory is never a public source.
        for directory in [stage/'data',stage/'bootstrap-chain']:
            if directory.exists():shutil.rmtree(directory)
        for directory in (stage/'modules').rglob('data'):
            if directory.is_dir():shutil.rmtree(directory)
        for directory in list(stage.rglob('__pycache__')):
            if directory.is_dir():shutil.rmtree(directory)
        for file in stage.rglob('*.pyc'):
            if file.is_file():file.unlink()
        subprocess.run([node,'distribution/verify.cjs','--release'],cwd=stage,env=build_env,check=True)
        files=write_manifest(stage)
        products=archives(stage,out)
        dest=out/'iz2s-platform'
        if dest.exists():shutil.rmtree(dest)
        shutil.copytree(stage,dest)
        shutil.copyfile(stage/'SOURCE-MANIFEST.json',out/'SOURCE-MANIFEST.json')
        shutil.copyfile(stage/'RELEASE-NOTES.md',out/'RELEASE-NOTES.md')
        print(json.dumps({'version':PROFILE['version'],'files':len(files)+1,'packages':products}))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=ROOT/'dist/github');parser.add_argument('--rebuild',action='store_true',help='Rebuild both launchers, all bundled modules and the ARM archive in the isolated staging tree')
    args=parser.parse_args();build(args.output,args.rebuild)
