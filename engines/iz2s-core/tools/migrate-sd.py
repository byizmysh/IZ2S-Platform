#!/usr/bin/env python3
"""Install the 0.25 SD layout and migrate a mounted legacy SD card."""
import argparse
import hashlib
import json
import os
import shutil
import stat
import tempfile
from pathlib import Path, PurePosixPath

RUNTIME = Path(__file__).resolve().parent.parent / 'runtime'
LEGACY = json.loads((RUNTIME.parents[2] / 'core/platform/legacy-core.json').read_text())


def digest(file):
    value = hashlib.sha256()
    with file.open('rb') as stream:
        for chunk in iter(lambda: stream.read(262144), b''):
            value.update(chunk)
    return value.hexdigest()


def files(directory):
    if not directory.exists():
        return []
    found = []
    for parent, directories, names in os.walk(directory, followlinks=False):
        for name in directories + names:
            item = Path(parent) / name
            mode = item.lstat().st_mode
            if stat.S_ISLNK(mode) or not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):
                raise ValueError('Unsupported SD entry: ' + str(item))
        found.extend(Path(parent) / name for name in names)
    return sorted(found)


def safe_target(root, relative):
    parts = PurePosixPath(relative).parts
    if not parts or parts[0] != 'iz2s' or any(part in ('.', '..') for part in parts):
        raise ValueError('Invalid IZ2S destination')
    target = root
    for part in parts:
        target = target / part
        if target.is_symlink():
            raise ValueError('Symlink at SD destination: ' + str(target))
        if target.exists() and target.stat().st_dev != root.stat().st_dev:
            raise ValueError('Destination is on another filesystem: ' + str(target))
    return target


def atomic_copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix='.' + target.name + '.', suffix='.part', dir=target.parent)
    temporary = Path(temporary)
    try:
        with os.fdopen(handle, 'wb') as out, source.open('rb') as stream:
            shutil.copyfileobj(stream, out, 262144)
            out.flush()
            os.fsync(out.fileno())
        if digest(temporary) != digest(source):
            raise ValueError('Copy verification failed: ' + str(target))
        temporary.replace(target)
        descriptor = os.open(target.parent, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        if digest(target) != digest(source):
            raise ValueError('Readback failed: ' + str(target))
    finally:
        temporary.unlink(missing_ok=True)


def save_receipt(file, value):
    temporary = file.with_suffix('.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(file)


def migration_target(relative):
    relative = LEGACY.get('sdFiles', {}).get(relative, relative)
    if relative.startswith('plugins/'):
        return 'iz2s/core/' + relative
    if relative in ('bin/game/main.npdm', 'bin/core/main.npdm'):
        return 'iz2s/core/' + relative
    if (relative.startswith(('bin/', 'plugin-packages/')) or
            relative in ('plugin-status.txt', 'nro-cleanup.txt')):
        return None  # Superseded runtime and logs are preserved in the PC backup.
    return 'iz2s/core/migrated/' + relative


def run(args):
    root = args.sd_root.absolute()
    if root.is_symlink() or not root.is_dir() or root == Path('/'):
        raise ValueError('Choose the root of the mounted SD card')
    root = root.resolve()
    names = LEGACY['sdRoots']
    candidates = [root / name for name in names if (root / name).exists()]
    if len(candidates) > 1:
        raise ValueError('Multiple legacy SD layouts found; back up and migrate each separately')
    legacy = candidates[0] if candidates else root / names[0]
    if legacy.is_symlink() or (legacy.exists() and not legacy.is_dir()):
        raise ValueError('Legacy entry must be a regular directory')
    backup = args.backup.resolve()
    if backup == root or backup.is_relative_to(root):
        raise ValueError('The backup must be outside the SD card')
    receipt_file = backup / 'migration.json'
    if args.finalize:
        if args.dry_run:
            raise ValueError('Use --finalize after preparing and testing the new session')
        if not args.session_confirmed:
            raise ValueError('Start and verify a new 0.25 console session, then add --session-confirmed')
        receipt = json.loads(receipt_file.read_text())
        if receipt.get('version') != 1 or receipt['sdRoot'] != str(root):
            raise ValueError('Receipt belongs to a different SD root')
        old_rows = {row['source']: row for row in receipt['legacy']}
        remaining = files(legacy)
        # Validate all remaining files before removing any. Retry handles partial cleanup.
        for file in remaining:
            relative = file.relative_to(legacy).as_posix()
            row = old_rows.get(relative)
            if not row or digest(file) != row['sha256']:
                raise ValueError('Legacy files changed; prepare migration again: ' + relative)
        for row in receipt['legacy']:
            if digest(backup / 'legacy' / row['source']) != row['sha256']:
                raise ValueError('Backup verification failed')
            if row['target'] and digest(safe_target(root, row['target'])) != row['sha256']:
                raise ValueError('Migrated file changed; preserve it before cleanup: ' + row['target'])
        for row in receipt['runtime']:
            if row['target'] and digest(safe_target(root, row['target'])) != row['sha256']:
                raise ValueError('The installed runtime differs from the prepared migration')
        receipt['state'] = 'cleanup-started'
        save_receipt(receipt_file, receipt)
        for file in remaining:
            if digest(file) != old_rows[file.relative_to(legacy).as_posix()]['sha256']:
                raise ValueError('Legacy file changed during cleanup')
            file.unlink()
        if legacy.exists():
            for parent, directories, names in os.walk(legacy, topdown=False):
                Path(parent).rmdir()
        receipt['state'] = 'complete'
        save_receipt(receipt_file, receipt)
        return {'state': 'complete', 'legacyDirectoryPresent': legacy.exists(), 'receipt': str(receipt_file)}

    expected = {row['path']: row for row in json.loads((RUNTIME / 'package-manifest.json').read_text())}
    layout = json.loads((RUNTIME / 'sd-layout.json').read_text())
    runtime_rows = []
    for entry in layout['files']:
        source = RUNTIME / 'package' / entry['source']
        row = expected[entry['source']]
        if source.stat().st_size != row['bytes'] or digest(source) != row['sha256']:
            raise ValueError('Runtime integrity mismatch: ' + entry['source'])
        target = entry['target'].lstrip('/')
        safe_target(root, target)
        runtime_rows.append(dict(source=entry['source'], target=target, sha256=row['sha256']))
    legacy_rows = []
    target_hashes = {}
    for source in files(legacy):
        relative = source.relative_to(legacy).as_posix()
        target = migration_target(relative)
        destination = safe_target(root, target) if target else None
        value = digest(source)
        if target:
            if target in target_hashes and target_hashes[target] != value:
                raise ValueError('Conflicting legacy SD files: ' + target)
            target_hashes[target] = value
        if destination and destination.exists() and digest(destination) != value:
            raise ValueError('Conflicting SD files: ' + target)
        legacy_rows.append(dict(source=relative, target=target, sha256=value))
    receipt = dict(version=1, state='prepared', sdRoot=str(root), legacy=legacy_rows, runtime=runtime_rows)
    if args.dry_run:
        return receipt
    backup.mkdir(parents=True, exist_ok=True)
    if receipt_file.exists():
        previous = json.loads(receipt_file.read_text())
        if previous['sdRoot'] != str(root):
            raise ValueError('Choose a separate backup for this SD card')
    # Preserve old files first; an interrupted preparation can safely be repeated.
    for row in legacy_rows:
        destination = backup / 'legacy' / row['source']
        if destination.exists() and digest(destination) != row['sha256']:
            raise ValueError('Legacy file differs from its existing backup; choose a new backup folder')
        if not destination.exists():
            atomic_copy(legacy / row['source'], destination)
    for row in runtime_rows:
        target = safe_target(root, row['target'])
        if target.exists() and digest(target) != row['sha256']:
            saved = backup / 'previous-runtime' / digest(target) / Path(row['target']).name
            if not saved.exists():
                atomic_copy(target, saved)
    save_receipt(receipt_file, receipt)
    for row in legacy_rows:
        if not row['target']:
            continue
        target = safe_target(root, row['target'])
        atomic_copy(backup / 'legacy' / row['source'], target)
    for row in runtime_rows:
        target = safe_target(root, row['target'])
        if not target.exists() or digest(target) != row['sha256']:
            atomic_copy(RUNTIME / 'package' / row['source'], target)
    for relative in ('iz2s/core/plugins', 'iz2s/core/plugin-packages', 'iz2s/log/core'):
        safe_target(root, relative).mkdir(parents=True, exist_ok=True)
    receipt['state'] = 'installed-awaiting-new-session' if legacy.exists() else 'installed'
    save_receipt(receipt_file, receipt)
    return {'state': receipt['state'], 'receipt': str(receipt_file), 'legacyDirectoryPresent': legacy.exists()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sd-root', type=Path, required=True)
    parser.add_argument('--backup', type=Path, required=True, help='Backup directory on the PC')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--finalize', action='store_true', help='Remove verified legacy files after a new session')
    parser.add_argument('--session-confirmed', action='store_true', help='A new 0.25 session has been verified on this SD')
    args = parser.parse_args()
    try:
        print(json.dumps(run(args), indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
