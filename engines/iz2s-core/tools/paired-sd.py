"""Use the launcher's manually opened FTP service for SD file operations."""
from ftplib import FTP, error_perm
from hashlib import sha256
from ipaddress import IPv4Address
from pathlib import Path, PurePosixPath
import json
import re
import sys


class ProfileRejected(Exception):
    pass


def safe_path(value):
    if not value.startswith('/') or any(c in value for c in '\r\n\x00\\:'):
        raise ValueError('Choose a valid path on the SD card.')
    parts = value.split('/')[1:] if value != '/' else []
    if any(not p or p in ('.', '..') or p.lower() in ('pc-link.key', 'pc-link.cfg')
           for p in parts):
        raise ValueError('This SD path is unavailable.')
    return value


def entries(ftp, path, include_staging=False):
    result = []
    for name, facts in ftp.mlsd(path):
        item = facts.get('type')
        if item not in ('dir', 'file') or name in ('.', '..'):
            continue
        if not name or re.search(r'[\x00-\x1f\x7f/\\:]', name) or (
            name.lower() in ('pc-link.key', 'pc-link.cfg') or (not include_staging and name.lower().startswith('.iz2s-browser-'))
        ):
            continue
        size = int(facts.get('size', '0')) if item == 'file' else 0
        if size < 0 or size >= 2 ** 53:
            raise ValueError('Unsupported file size.')
        result.append({'name': name, 'type': item, 'size': size})
        if len(result) > 16384:
            raise ValueError('Directory exceeds 16,384 entries.')
    return result


def entry(ftp, path, include_staging=False):
    if path == '/':
        return {'name': '', 'type': 'dir', 'size': 0}
    where = PurePosixPath(path)
    return next((item for item in entries(ftp, str(where.parent), include_staging)
                 if item['name'].casefold() == where.name.casefold()), None)


def stage_path(value):
    path = safe_path(value)
    if not re.fullmatch(r'\.iz2s-browser-[0-9a-f]{32}', PurePosixPath(path).name):
        raise ValueError('Invalid transfer staging name.')
    return path


def file_digest(ftp, path):
    digest = sha256()

    def progress(data):
        digest.update(data)
        print('+', file=sys.stderr, flush=True)

    ftp.retrbinary('RETR ' + path, progress, blocksize=262144)
    return digest.hexdigest()


def remove_nro_folder(ftp, executable, size):
    if not re.fullmatch(r'/iz2s/modules/[a-z0-9][a-z0-9-]{0,46}/launcher\.nro', executable):
        raise ValueError('Select an installed NRO.')
    root = str(PurePosixPath(executable).parent)
    current = entry(ftp, executable)
    if not current or current['type'] != 'file' or current['size'] != size:
        raise ValueError('The installed NRO changed. Refresh the library and retry.')
    pending = [(root, 0)]
    folders, files = [], []
    while pending:
        folder, depth = pending.pop()
        if depth > 64:
            raise ValueError('The application folder is nested too deeply.')
        folders.append(folder)
        print('+', file=sys.stderr, flush=True)
        for name, facts in ftp.mlsd(folder):
            kind = facts.get('type')
            if name in ('.', '..') or kind in ('cdir', 'pdir'):
                continue
            if not name or len(name) > 255 or re.search(r'[\x00-\x1f\x7f/\\:]', name):
                raise ValueError('The application folder contains an unsafe entry.')
            child = folder + '/' + name
            if kind == 'dir':
                pending.append((child, depth + 1))
            elif kind == 'file':
                files.append(child)
            else:
                raise ValueError('The application folder contains an unsafe entry.')
            if len(pending) + len(folders) + len(files) > 65536:
                raise ValueError('The application folder contains too many entries.')
    for file in files:
        if file.casefold() != executable.casefold():
            ftp.delete(file)
            print('+', file=sys.stderr, flush=True)
    for folder in reversed(folders[1:]):
        ftp.rmd(folder)
        print('+', file=sys.stderr, flush=True)
    ftp.delete(executable)
    ftp.rmd(root)
    return True


def operation(ftp, op, path, extra):
    if op == 'probe':
        ftp.cwd('/')
        return True
    if op == 'list':
        return entries(ftp, path)
    if op == 'stat':
        return entry(ftp, path)
    if op == 'mkdir':
        if extra and extra[0] == 'parents':
            current = ''
            for part in path.split('/')[1:]:
                current += '/' + part
                item = entry(ftp, current)
                if item is None:
                    ftp.mkd(current)
                elif item['type'] != 'dir':
                    raise ValueError('A file already uses this name.')
        else:
            ftp.mkd(path)
        return True
    if op == 'rename':
        dest = safe_path(extra[0])
        if dest == '/' or entry(ftp, dest):
            raise ValueError('The destination already exists.')
        ftp.rename(path, dest)
        return True
    if op == 'remove-nro':
        return remove_nro_folder(ftp, path, int(extra[0]))
    if op == 'delete':
        item = entry(ftp, path)
        if item is None:
            raise ValueError('The selected entry no longer exists.')
        if item['type'] == 'file':
            ftp.delete(path)
        else:
            ftp.rmd(path)
        return True
    if op == 'download-stream':
        expected = int(extra[0])
        received = 0

        def append(data):
            nonlocal received
            received += len(data)
            if received > expected:
                raise ValueError('The file changed during FTP transfer.')
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()

        ftp.retrbinary('RETR ' + path, append, blocksize=65536)
        if received != expected:
            raise ValueError('FTP download was incomplete.')
        return None
    if op == 'upload-stream':
        stage = stage_path(path)
        if entry(ftp, stage, True):
            raise ValueError('Transfer staging file already exists.')
        expected = int(extra[0])
        sent = 0

        def progress(data):
            nonlocal sent
            sent += len(data)
            if sent > expected:
                raise ValueError('Invalid upload size.')
            print('+', file=sys.stderr, flush=True)

        ftp.storbinary('STOR ' + stage, sys.stdin.buffer, blocksize=65536, callback=progress)
        if sent != expected:
            raise ValueError('FTP upload was incomplete.')
        return {'size': sent}
    if op == 'verify-stage':
        stage = stage_path(path)
        info = entry(ftp, stage, True)
        if not info or info['type'] != 'file' or info['size'] != int(extra[0]):
            raise ValueError('FTP upload readback size differs.')
        if len(extra) > 1 and file_digest(ftp, stage) != extra[1]:
            raise ValueError('FTP upload readback checksum differs.')
        return True
    if op == 'publish':
        stage = stage_path(extra[0])
        if PurePosixPath(stage).parent != PurePosixPath(path).parent:
            raise ValueError('The staging and destination folders differ.')
        if entry(ftp, path, True):
            raise ValueError('The destination already exists.')
        ftp.rename(stage, path)
        return True
    if op == 'digest':
        return file_digest(ftp, path)
    if op == 'discard-stage':
        stage = stage_path(path)
        if entry(ftp, stage, True):
            ftp.delete(stage)
        return True
    raise ValueError('Unknown SD operation.')


def main():
    host, op, path, *extra = sys.argv[1:]
    ip = str(IPv4Address(host))
    path = safe_path(path)
    if path == '/' and op not in ('probe', 'list', 'stat'):
        raise ValueError('Select a file or folder inside the SD card.')
    staging = ('upload-stream', 'verify-stage', 'discard-stage')
    if op not in staging and any(part.lower().startswith('.iz2s-browser-') for part in PurePosixPath(path).parts):
        raise ValueError('This SD path is unavailable.')
    with FTP(timeout=2 if op == 'probe' else 60 if op in (*staging, 'download-stream', 'digest') else 12) as ftp:
        ftp.trust_server_pasv_ipv4_address = False
        ftp.connect(ip, 1425)
        try:
            ftp.login('anonymous', '')
        except error_perm as error:
            if str(error)[:3] == '530':
                raise ProfileRejected from error
            raise
        result = operation(ftp, op, path, extra)
    if op != 'download-stream':
        print(json.dumps(result, ensure_ascii=True))


if __name__ == '__main__':
    try:
        main()
    except ProfileRejected:
        sys.exit(53)
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
