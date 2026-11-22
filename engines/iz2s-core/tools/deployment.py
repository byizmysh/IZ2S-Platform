"""FTP publication with a local backup and readback; dry-run never connects."""
from pathlib import Path, PurePosixPath
import argparse
import datetime
import ftplib
import hashlib
from ipaddress import IPv4Address
import json
import os
import subprocess
import uuid
from game_package import parse as parse_game_package, build as build_game_package

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT.parents[1] / 'data/core'


def ensure_directory(client, directory):
    if directory == '/':
        return
    previous = client.pwd()
    try:
        client.cwd(directory)
    except ftplib.error_perm as error:
        if not str(error).startswith('550'):
            raise
        client.mkd(directory)
        client.cwd(directory)
    finally:
        client.cwd(previous)


def valid_pc_link_config(payload):
    if len(payload) > 160 or not payload.endswith(b'\n'):
        return False
    try:
        primary, port, *alternates = payload.decode('ascii').splitlines()
        return (port == '39524' and len(alternates) <= 7
                and all(int(IPv4Address(host)) for host in (primary, *alternates)))
    except (UnicodeError, ValueError):
        return False


def paired_credentials(host=None, port=None):
    command = ['node', str(ROOT / 'tools/ftp-connect.cjs')]
    if host:
        command += ['--host', host]
    if port is not None:
        command += ['--port', str(port)]
    return json.loads(subprocess.check_output(command, text=True))


def main(program, destination, argv=None, ftp_factory=ftplib.FTP, *, source_default=None, kind="nro", initial=False, same_release=None, expected_sha256=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', help='OunceShell FTP host; required for publication')
    parser.add_argument('--port', type=int, help='FTP port (default 1425; paired mode auto-detects)')
    parser.add_argument('--source', type=Path, default=source_default or ROOT / 'runtime/package/programs' / program)
    parser.add_argument('--out', type=Path, default=Path(os.environ.get('IZ2S_CORE_OUT', DEFAULT_OUT)))
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--paired', action='store_true', help='Use the existing IZ2s FTP pairing')
    args = parser.parse_args(argv)
    if not args.dry_run and not (args.host or args.paired):
        parser.error('--host is required for OunceShell FTP')
    source, out = args.source.resolve(), args.out.resolve()
    if not (source.is_relative_to(ROOT) or source.is_relative_to(out)):
        parser.error('Source must be inside engines/iz2s-core or OUT')
    payload = source.read_bytes()
    valid = (kind == 'nro' and payload[16:20] == b'NRO0' or
             kind == 'nso' and payload[:4] == b'NSO0' or
             kind == 'iza' and payload[:8] == b'IZSDAPP1' or
             kind == 'pc-link-config' and valid_pc_link_config(payload) or
             kind == 'pfs0' and bool(parse_game_package(payload)) or
             kind == 'romfs' and len(payload) >= 80 and int.from_bytes(payload[:8], 'little') == 80)
    if not valid:
        raise ValueError('Invalid ' + kind.upper() + ': ' + str(source))
    digest = hashlib.sha256(payload).hexdigest()
    tag = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    folder = out / 'logs' / ('ftp-' + Path(destination).stem + '-' + tag)
    folder.mkdir(parents=True, exist_ok=False)
    report_file = folder / 'receipt.json'
    staging, backup = destination + '.uploading-' + tag, destination + '.before-' + tag + '.bak'
    if args.port is None and (not args.paired or args.dry_run):
        args.port = 1425
    report = dict(source=str(source), destination=destination, staging=staging, backup=backup,
                  sha256=digest, bytes=len(payload), host=args.host, port=args.port)

    def save(state):
        report['state'] = state
        temporary = report_file.with_suffix('.tmp')
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(report_file)

    if args.dry_run:
        save('dry-run')
    else:
        credentials = None
        if args.paired:
            credentials = paired_credentials(args.host, args.port)
            args.port = report['port'] = credentials['port']
            report['host'] = credentials['host']

        def connect():
            client = ftp_factory()
            try:
                client.connect(report['host'], args.port, timeout=15)
                if credentials:
                    client.login(credentials['user'], credentials['password'])
                else:
                    client.login()
                return client
            except Exception:
                client.close()
                raise

        def readback(client, remote):
            digest = hashlib.sha256()
            client.retrbinary('RETR ' + remote, digest.update, 262144)
            return digest.hexdigest()

        def verify(client, remote, expected):
            if readback(client, remote) != expected:
                raise ValueError('Readback mismatch: ' + remote)

        def recover():
            client = connect()
            try:
                name = PurePosixPath(destination)
                directory = {PurePosixPath(item).name for item in client.nlst(str(name.parent))}
                if name.name not in directory:
                    if report['previousSha256'] is None:
                        save('publication-stopped')
                        return
                    verify(client, backup, report['previousSha256'])
                    client.rename(backup, destination)
                    verify(client, destination, report['previousSha256'])
                    save('previous-restored')
                    return
                current = readback(client, destination)
                if current == digest:
                    save('installed-and-verified')
                elif current == report['previousSha256']:
                    save('previous-preserved')
                else:
                    raise ValueError('Unexpected file at the destination during recovery')
            finally:
                client.close()

        save('connecting')
        ftp = None
        try:
            ftp = connect()
            previous = folder / ('previous.' + kind)
            exists = True
            if initial:
                parent = str(PurePosixPath(destination).parent)
                for directory in reversed(PurePosixPath(parent).parents):
                    ensure_directory(ftp, str(directory))
                ensure_directory(ftp, parent)
                entries = ftp.nlst(parent)
                exists = PurePosixPath(destination).name in {PurePosixPath(item).name for item in entries}
            report['previousSha256'] = None
            if exists:
                with previous.open('xb') as stream:
                    ftp.retrbinary('RETR ' + destination, stream.write, 262144)
                report['previousLocalBackup'] = str(previous)
                report['previousSha256'] = hashlib.sha256(previous.read_bytes()).hexdigest()
            if expected_sha256 is not None and report.get('previousSha256') != expected_sha256:
                raise ValueError('The installed launcher package changed during preparation; retry the update')
            if report['previousSha256'] == digest or (exists and same_release and same_release(source, previous)):
                save('already-installed')
            else:
                save('upload-in-flight')
                with source.open('rb') as stream:
                    ftp.storbinary('STOR ' + staging, stream, 262144)
                verify(ftp, staging, digest)
                if exists:
                    verify(ftp, destination, report['previousSha256'])
                    save('backup-rename-in-flight')
                    ftp.rename(destination, backup)
                    verify(ftp, backup, report['previousSha256'])
                elif PurePosixPath(destination).name in {PurePosixPath(item).name for item in ftp.nlst(str(PurePosixPath(destination).parent))}:
                    raise ValueError('Destination appeared during publication: ' + destination)
                save('publish-rename-in-flight')
                ftp.rename(staging, destination)
                verify(ftp, destination, digest)
                save('installed-and-verified')
            ftp.quit()
        except Exception as error:
            report['error'], report['lastState'] = str(error), report['state']
            if report['lastState'] in ('backup-rename-in-flight', 'publish-rename-in-flight'):
                ftp.close()
                ftp = None
                try:
                    recover()
                except Exception as recovery_error:
                    report['recoveryError'] = str(recovery_error)
                    save('failed-inspect-before-retry')
            elif report['lastState'] not in ('already-installed', 'installed-and-verified'):
                save('publication-stopped')
            if report['state'] not in ('already-installed', 'installed-and-verified'):
                raise
        finally:
            if ftp is not None:
                ftp.close()
    print(json.dumps(dict(state=report['state'], report=str(report_file), sha256=digest)))
    return report


def prepare_game_container(args, package, ftp_factory=ftplib.FTP):
    """An already running IZ2S mounts exefs.nsp, not the loose main file."""
    if args.dry_run:
        return None
    target = '/iz2s/core/bin/game/exefs.nsp'
    credentials = None
    if args.paired:
        credentials = paired_credentials(args.host, args.port)
        args.port = credentials['port']
    host = credentials['host'] if credentials else args.host
    if not host:
        raise ValueError('Specify --host or --paired')
    client = ftp_factory()
    try:
        client.connect(host, args.port, timeout=15)
        client.login(credentials['user'], credentials['password']) if credentials else client.login()
        try:
            entries = client.nlst(str(PurePosixPath(target).parent))
        except ftplib.error_perm as error:
            if str(error).startswith('550'):
                return None  # First IZ2S launch creates its console-specific policy.
            raise
        if 'exefs.nsp' not in {PurePosixPath(name).name for name in entries}:
            return None
        contents = bytearray()
        client.retrbinary('RETR ' + target, contents.extend, 262144)
        current = parse_game_package(contents)
    finally:
        client.close()
    image = (package / 'game/main').read_bytes()
    if current['main'] == image:
        return None
    folder = args.out.resolve() / 'runtime-packages' / uuid.uuid4().hex
    folder.mkdir(parents=True)
    source = folder / 'exefs.nsp'
    source.write_bytes(build_game_package(image, current['main.npdm']))
    return source, target, hashlib.sha256(contents).hexdigest()


def main_runtime(argv=None, ftp_factory=ftplib.FTP):
    """Publish the IZ2S-integrated runtime; the menu assets precede the native components."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host')
    parser.add_argument('--port', type=int, help='FTP port (default 1425; paired mode auto-detects)')
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    parser.add_argument('--package', type=Path, default=ROOT / 'runtime/package')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--paired', action='store_true')
    args = parser.parse_args(argv)
    package = args.package.resolve()
    expected = {row['path']: row for row in json.loads((ROOT / 'runtime/package-manifest.json').read_text())}
    layout = json.loads((ROOT / 'runtime/sd-layout.json').read_text())
    rows = [(entry['source'], entry['target'], entry['kind']) for entry in layout['files']]
    # Complete preflight before any connection or mutation.
    for name, destination, kind in rows:
        payload = (package / name).read_bytes()
        if len(payload) != expected[name]['bytes'] or hashlib.sha256(payload).hexdigest() != expected[name]['sha256']:
            raise ValueError('Runtime package integrity mismatch: ' + name)
    if args.paired and not args.dry_run:
        # Keep a multi-file publication on the same address and port.
        credentials = paired_credentials(args.host, args.port)
        args.host, args.port = credentials['host'], credentials['port']
    elif args.port is None:
        args.port = 1425
    common = ['--port', str(args.port), '--out', str(args.out)]
    if args.host: common += ['--host', args.host]
    if args.paired: common += ['--paired']
    if args.dry_run: common += ['--dry-run']
    container = prepare_game_container(args, package, ftp_factory)
    results = []
    for name, destination, kind in rows:
        results.append(main(name, destination, common, ftp_factory,
                            source_default=package / name, kind=kind, initial=True))
        if name == 'game/main' and container:
            source, target, previous = container
            results.append(main('exefs.nsp', target, common, ftp_factory,
                                source_default=source, kind='pfs0', initial=True,
                                expected_sha256=previous))
    return results
