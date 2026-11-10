#!/usr/bin/env python3
"""Render the real IZPSX native menus locally with synthetic controller state."""
from pathlib import Path
import os,subprocess,tempfile,json,hashlib
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/images';OUT.mkdir(parents=True,exist_ok=True)
common=['-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-Isrc','-Isrc/sd-runtime']
cases={'pause':['tools/check-pause-menu.c','src/native-menu.c'],'settings':['tests/settings-model.c','src/settings.c','src/settings-menu.c','src/native-menu.c','src/memory-card.c','src/fs-file.c','src/fs-dir.c','src/fs-ipc.c']}
with tempfile.TemporaryDirectory(prefix='izpsx-ui-') as d:
 tmp=Path(d)
 for name,files in cases.items():
  binary=tmp/name
  subprocess.run([os.environ.get('CC','cc'),*common,*files,'-Wl,--gc-sections','-o',str(binary)],cwd=ROOT,check=True)
  subprocess.run([str(binary),str(tmp/'pause.rgba') if name=='pause' else str(tmp)],cwd=ROOT,check=True)
 for source,target in [('pause.rgba','pause-menu.png'),('settings-four-players.rgba','controller-profiles.png')]:
  Image.frombytes('RGBA',(640,480),(tmp/source).read_bytes()).save(OUT/target)
 hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sorted(set(sum(cases.values(),[])))}
 (OUT/'provenance.json').write_text(json.dumps({'version':'0.5.1','renderer':'unmodified native IZPSX menu renderer','sourceHashes':hashes,'controllerState':'synthetic test profiles','consoleCapture':False,'gameExecuted':False},indent=2)+'\n')
