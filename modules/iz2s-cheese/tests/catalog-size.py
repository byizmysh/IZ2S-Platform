#!/usr/bin/env python3
"""Exercise old and new thumbnail packs and catalog sizes beyond prior ceilings."""
from pathlib import Path
import os, struct, subprocess, tempfile
module = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='store-catalog-size-') as folder:
    work = Path(folder); binary = work / 'check'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(module/'switch'),
        module/'tests/catalog-size.c',*(module/'switch'/x for x in ['store.c','ui.c','download.c','installed-artwork.c']),
        '-lssl','-lcrypto','-o',binary],check=True)
    for count, legacy in [(25, True), (33, True), (256, False), (1024, False)]:
        sd=work/'sd'; catalog=sd/'iz2s/modules/iz2s-cheese/catalog';catalog.mkdir(parents=True,exist_ok=True)
        rows=['IZSTORE2']
        with (catalog/'artwork.bin').open('wb') as art:
            art.write((b'IZART1\n'+bytes([count])) if legacy else (b'IZART2\n'+struct.pack('<I',count)))
            for i in range(count):
                name=f'app-{i}'
                rows += [f'app\t{name}\tnro\t1.0\tApp {i}\tExample\tTools\tApplication.',
                         'file\t4096\t'+'a'*64+'\tlauncher.nro',
                         'github\thttps://github.com/example/app/releases/download/v1.0/app.nro']
                art.write(name.encode().ljust(48,b'\0')); art.write(struct.pack('<I',0xff336699)*(96*96))
        (catalog/'catalog.tsv').write_text('\n'.join(rows)+'\n')
        subprocess.run([binary,sd,str(count)],check=True)
print('25/33-app legacy packs and 256/1024-app IZART2 packs: ASan/UBSan PASS')
