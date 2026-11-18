"""Installer-only runtime copy, with the same inventory as the portable installer."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1]
def copy_sources(stage):
    stage=Path(stage)
    if stage.resolve()==ROOT or ROOT in stage.resolve().parents:raise RuntimeError('Choose a destination outside the source')
    manifest=json.loads((ROOT/'distribution/files.json').read_text())
    if manifest.get('format')!='iz2s.runtime-distribution' or manifest.get('version')!=1 or not isinstance(manifest.get('files'),dict):raise RuntimeError('Invalid runtime inventory')
    def checked(name,row):
        if not name or any(c in name for c in ('\\',':')) or any(ord(c)<32 for c in name) or any(x in ('','.','..') for x in name.split('/')):raise RuntimeError('Invalid runtime path')
        if not isinstance(row,dict) or type(row.get('bytes')) is not int or row['bytes']<0 or type(row.get('executable')) is not bool or not isinstance(row.get('sha256'),str) or len(row['sha256'])!=64 or any(c not in '0123456789abcdef' for c in row['sha256']):raise RuntimeError('Invalid runtime entry')
        source=ROOT/name
        if source.is_symlink() or not source.is_file():raise RuntimeError('Invalid runtime file: '+name)
        content=source.read_bytes()
        if len(content)!=row['bytes'] or hashlib.sha256(content).hexdigest()!=row['sha256']:raise RuntimeError('Runtime integrity failure: '+name)
        return content
    # Match the portable installer: reject an incomplete source before copying
    # any entry. Recheck bytes during publication if the source changed meanwhile.
    for name,row in manifest['files'].items():checked(name,row)
    for name,row in manifest['files'].items():
        content=checked(name,row)
        dest=stage/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(content);dest.chmod(0o755 if row['executable'] else 0o644)
    shutil.copyfile(ROOT/'distribution/files.json',stage/'distribution/files.json')
    for name in ('data','dist'):(stage/name).mkdir(exist_ok=True)
