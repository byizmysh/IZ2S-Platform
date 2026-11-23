#!/usr/bin/env python3
from pathlib import Path
import hashlib, os, subprocess, sys, tempfile
from zipfile import ZipFile
module=Path(__file__).resolve().parent.parent
release=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='store-self-update-') as name:
    work=Path(name)
    for kind in ['iza','nro']:
        sd=work/kind
        folder=sd/('iz2s/modules/iz2s-cheese' if kind=='iza' else 'switch/iz2s-cheese')
        folder.mkdir(parents=True)
        active=folder/('launcher.iza' if kind=='iza' else 'iz2s-cheese.nro')
        previous=b'previous executable';active.write_bytes(previous)
        catalog=sd/'iz2s/iz2s-cheese/catalog.tsv';catalog.parent.mkdir(parents=True)
        program=release/f'iz2s-cheese.{kind}'
        files=[('launcher.'+kind, program)]
        if kind=='iza':
            with ZipFile(release/'iz2s-cheese-iza-sd.zip') as z:
                for p in z.namelist():
                    if p.endswith('.izdata'):files.append((Path(p).name,release/Path(p).name))
        rows=['IZSTORE3',f'app\tiz2s-cheese\t{kind}\t0.8.2\tIZ2S Cheese\tIZMYSH\tTools\tHomebrew store.']
        rows += [f'file\t{p.stat().st_size}\t{hashlib.sha256(p.read_bytes()).hexdigest()}\t{n}' for n,p in files]
        rows += [f'github\thttps://github.com/byizmysh/iz2s-cheese/releases/download/v0.8.2/iz2s-cheese.{kind}']
        other='nro' if kind=='iza' else 'iza'
        other_program=release/f'iz2s-cheese.{other}'
        other_files=[('launcher.'+other,other_program)]
        if other=='iza':
            with ZipFile(release/'iz2s-cheese-iza-sd.zip') as z:
                for p in z.namelist():
                    if p.endswith('.izdata'):other_files.append((Path(p).name,release/Path(p).name))
        rows += [f'app\tiz2s-cheese\t{other}\t0.8.2\tIZ2S Cheese\tIZMYSH\tTools\tHomebrew store.']
        rows += [f'file\t{p.stat().st_size}\t{hashlib.sha256(p.read_bytes()).hexdigest()}\t{n}' for n,p in other_files]
        rows += [f'github\thttps://github.com/byizmysh/iz2s-cheese/releases/download/v0.8.2/iz2s-cheese.{other}']
        catalog.write_text('\n'.join(rows)+'\n')
        binary=work/('test-'+kind)
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
            *(['-DIZ_STORE_STANDALONE'] if kind=='nro' else []),'-I'+str(module/'switch'),
            module/'tests/self-update.c',*(module/'switch'/f for f in ['store.c','ui.c','download.c','installed-artwork.c']),
            '-Wl,--wrap=github_download','-lssl','-lcrypto','-o',binary],check=True)
        subprocess.run([binary,sd],env={**os.environ,'STORE_TEST_RELEASE':str(release)},check=True)
        assert active.read_bytes()==program.read_bytes()
        assert (folder/f'.store-previous.{kind}.bak').read_bytes()==previous
        for n,p in files[1:]:assert (folder/n).read_bytes()==p.read_bytes()
