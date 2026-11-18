from pathlib import Path
import json,hashlib,tarfile,zipfile,subprocess,os,stat,re,gzip,argparse
parser=argparse.ArgumentParser();parser.add_argument('--workspace',type=Path,required=True);args=parser.parse_args();W=args.workspace.resolve();out=W/'archives';out.mkdir(exist_ok=True);(W/'reports').mkdir(exist_ok=True);reports=[]
for stage in sorted((W/'staging').iterdir()):
 target=json.loads((stage/'distribution/profile.json').read_text())['releaseTarget']
 result=subprocess.run(['npm','run','check'],cwd=stage,text=True,capture_output=True)
 (W/'reports'/('check-'+target+'.txt')).write_text(result.stdout+result.stderr);assert result.returncode==0,result.stdout+result.stderr
 inventory=json.loads((stage/'distribution/files.json').read_text())['files']
 files=sorted([f for f in stage.rglob('*') if f.is_file()]);assert len(files)==len(inventory)+1
 assert not list((stage/'data').iterdir()) and not list((stage/'dist').iterdir())
 assert not any(f.is_symlink() for f in stage.rglob('*'))
 forbidden=[f.relative_to(stage).as_posix() for f in files if any(x in f.relative_to(stage).parts for x in ['.git','.versions','__pycache__','tests','test-data']) or f.suffix.lower() in ['.core','.log','.key','.pem','.pyc']];assert not forbidden,forbidden
 for f in files:
  if f!=stage/'distribution/files.json':
   v=inventory[f.relative_to(stage).as_posix()];assert hashlib.sha256(f.read_bytes()).hexdigest()==v['sha256']
 if target=='linux':
  name=stage.name+'.tar.gz';p=out/name
  with p.open('wb') as output, gzip.GzipFile(filename='',fileobj=output,mode='wb',compresslevel=6,mtime=1794873600) as compressed, tarfile.open(fileobj=compressed,mode='w',format=tarfile.PAX_FORMAT) as tar:
   for f in [stage,*sorted(stage.rglob('*'))]:
    arc=stage.name+(('/'+f.relative_to(stage).as_posix()) if f!=stage else '')
    info=tar.gettarinfo(str(f),arcname=arc);info.uid=info.gid=0;info.uname=info.gname='';info.mtime=1794873600
    if f.is_file():
     with f.open('rb') as b:tar.addfile(info,b)
    else:tar.addfile(info)
  with tarfile.open(p,'r:gz') as tar:
   for f in files:
    arc=stage.name+'/'+f.relative_to(stage).as_posix();assert hashlib.sha256(tar.extractfile(arc).read()).digest()==hashlib.sha256(f.read_bytes()).digest()
 else:
  name=stage.name+'.zip';p=out/name
  with zipfile.ZipFile(p,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
   for f in files:
    info=zipfile.ZipInfo(stage.name+'/'+f.relative_to(stage).as_posix(),(2026,11,17,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=(stat.S_IFREG|0o644)<<16;z.writestr(info,f.read_bytes())
   for d in ['data','dist']:
    info=zipfile.ZipInfo(stage.name+'/'+d+'/',(2026,11,17,0,0,0));info.external_attr=((stat.S_IFDIR|0o755)<<16)|0x10;z.writestr(info,b'')
  with zipfile.ZipFile(p) as z:
   assert z.testzip() is None
   for f in files:assert hashlib.sha256(z.read(stage.name+'/'+f.relative_to(stage).as_posix())).digest()==hashlib.sha256(f.read_bytes()).digest()
 digest=hashlib.sha256(p.read_bytes()).hexdigest()
 evidence=W/'installation-tests'/('linux' if target=='linux' else 'windows')/'result.json'
 tested=json.loads(evidence.read_text(encoding='utf-8-sig')) if evidence.exists() else {}
 qualified=tested.get('passed') is True and tested.get('archiveSha256')==digest
 report={'archive':name,'bytes':p.stat().st_size,'sha256':digest,'files':len(files),'unpackedBytes':sum(f.stat().st_size for f in files),'target':target,'moduleCount':6,'archiveReadbackVerified':True,'check':'passed','nativeHardwareTested':False,'installerTestPassed':qualified,'windowsInstallerExecuted':target=='windows-x64' and qualified}
 reports.append(report);print(json.dumps(report),flush=True)
(W/'archives/SHA256SUMS.txt').write_text(''.join(x['sha256']+'  '+x['archive']+'\n' for x in reports))
(W/'reports/artifacts.json').write_text(json.dumps(reports,indent=2)+'\n')
