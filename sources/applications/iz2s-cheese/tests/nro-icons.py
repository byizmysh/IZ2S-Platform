#!/usr/bin/env python3
"""Check NRO icon publication without a catalog thumbnail or UI redraw."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile

module = Path(__file__).resolve().parent.parent
release = Path(sys.argv[1]).resolve()
program = (release / 'iz2s-cheese.nro').read_bytes()
digest = hashlib.sha256(program).hexdigest()
with tempfile.TemporaryDirectory(prefix='store-nro-icons-') as folder:
    work = Path(folder)
    binary = work / 'host-check'
    subprocess.run([
        'cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        module / 'host-check.c',
        *(module / 'switch' / name for name in
          ['store.c', 'ui.c', 'download.c', 'installed-artwork.c']),
        '-lssl', '-lcrypto', '-o', binary,
    ], check=True)
    sd = work / 'sd'
    config = sd / 'iz2s/iz2s-cheese'
    payload = config / 'payloads/sample' / digest / 'launcher.nro'
    payload.parent.mkdir(parents=True)
    payload.write_bytes(program)
    (config / 'catalog.tsv').write_text(
        'IZSTORE1\napp\tsample\tnro\t1.0\tSample\tExample\tTools\tTest app.\n'
        f'file\t{len(program)}\t{digest}\tlauncher.nro\n'
    )
    subprocess.run([binary, sd, 'raw-install', 'sample'], check=True)
    icon = sd / 'iz2s/modules/sample/.izcheese-icon.rgba'
    pixels = icon.read_bytes()
    assert len(pixels) == 96 * 96 * 4 and len(set(pixels)) > 4
    subprocess.run([binary, sd, 'raw-remove', 'sample'], check=True)
    assert not icon.exists()
print('NRO icon saved before install returns and removed with application: PASS')
