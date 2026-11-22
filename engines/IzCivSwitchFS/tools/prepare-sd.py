"""Add the two missing RUN25 bootstrap files using the launcher's open FTP.

Existing files must match. No launcher, library, save or running process is changed.
"""
from ftplib import FTP, error_perm
from hashlib import sha256
from io import BytesIO
from pathlib import Path
from ipaddress import IPv4Address
import json
import sys
import uuid


def prepare(host, verify_only=False):
    IPv4Address(host)
    runtime = Path(__file__).resolve().parents[2] / 'iz2s-core' / 'runtime'
    records = {x['path']: x for x in json.loads((runtime / 'package-manifest.json').read_text())}
    files = [('bootstrap.bin', '/iz2s/core/bin/bootstrap.bin'),
             ('ro/main', '/iz2s/core/bin/ro/main')]
    with FTP() as ftp:
        ftp.connect(host, 1425, timeout=15)
        ftp.login('anonymous', '')
        ftp.voidcmd('TYPE I')
        # Verify that this is the existing IZ2S installation before writing.
        for source, target in [('core/main', '/iz2s/core/bin/core/main'),
                               ('game/main', '/iz2s/core/bin/game/main'),
                               ('iz2s/izlauncher.iza', '/iz2s/izlauncher.iza'),
                               ('iz2s/izlauncher.romfs', '/iz2s/izlauncher.romfs')]:
            if ftp.size(target) != records[source]['bytes']:
                raise ValueError('IZ2S file missing or wrong size: ' + target)
        if not ftp.size('/iz2s/core/bin/core/exefs.nsp'):
            raise ValueError('IZ2S core package is missing')
        for source, target in files:
            content = (runtime / 'package' / source).read_bytes()
            record = records[source]
            digest = sha256(content).hexdigest()
            if len(content) != record['bytes'] or digest != record['sha256']:
                raise ValueError('Local runtime integrity mismatch: ' + source)
            # Listing the already existing directory distinguishes missing files
            # from an inaccessible SD card without interpreting every 550 as absent.
            directory, name = target.rsplit('/', 1)
            entries = dict(ftp.mlsd(directory))
            if name in entries:
                remote = BytesIO()
                ftp.retrbinary('RETR ' + target, remote.write)
                if sha256(remote.getvalue()).hexdigest() != digest:
                    raise ValueError('Existing file differs; not overwritten: ' + target)
                print('Already verified: ' + target)
                continue
            if verify_only:
                raise ValueError('Required file missing: ' + target)
            temporary = directory + '/.iz2sciv-' + uuid.uuid4().hex
            try:
                ftp.storbinary('STOR ' + temporary, BytesIO(content))
                remote = BytesIO()
                ftp.retrbinary('RETR ' + temporary, remote.write)
                if sha256(remote.getvalue()).hexdigest() != digest:
                    raise ValueError('FTP verification failed: ' + target)
                if name in dict(ftp.mlsd(directory)):
                    raise ValueError('Target appeared during upload: ' + target)
                ftp.rename(temporary, target)
            except BaseException:
                try:
                    ftp.delete(temporary)
                except error_perm:
                    pass
                raise
            print('Added and SHA-256 verified: ' + target)
        print('IZ2SCIV_SD_VERIFIED')


if __name__ == '__main__':
    prepare(sys.argv[1], verify_only='--verify-only' in sys.argv[2:])
