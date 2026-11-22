"""Persist only the selected radio's restoration recipe before releasing it."""
import json
import os
from pathlib import Path
import re
import sys
import tempfile


def read(path):
    value = json.loads(Path(path).read_text())
    if value.get('version') != 1 or not re.fullmatch(r'phy\d+', value.get('phy', '')):
        raise ValueError('Invalid Wi-Fi recovery record')
    rows = value.get('interfaces', [])
    if not rows or len(rows) > 16:
        raise ValueError('Invalid Wi-Fi recovery interfaces')
    for row in rows:
        if (not re.fullmatch(r'[A-Za-z0-9_-]{1,15}', row.get('name', ''))
                or not re.fullmatch(r'(?:[0-9a-f]{2}:){5}[0-9a-f]{2}', row.get('mac', ''))
                or row.get('managed') not in ('yes', 'no') or row.get('up') not in ('0', '1')
                or not re.fullmatch(r'(?:[0-9a-fA-F-]{36})?', row.get('connection', ''))):
            raise ValueError('Invalid Wi-Fi recovery interface')
    return value


def save(path, phy, values):
    if len(values) % 5:
        raise ValueError('Incomplete Wi-Fi recovery record')
    rows = [dict(zip(('name', 'connection', 'managed', 'up', 'mac'), values[i:i+5]))
            for i in range(0, len(values), 5)]
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.civ7-network-', dir=target.parent)
    try:
        with os.fdopen(fd, 'w') as output:
            json.dump({'version': 1, 'phy': phy, 'interfaces': rows}, output)
            output.flush()
            os.fsync(output.fileno())
        read(temporary)
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)


if __name__ == '__main__':
    action, filename, *args = sys.argv[1:]
    if action == 'save':
        save(filename, args[0], args[1:])
    elif action == 'show':
        saved = read(filename)
        print(saved['phy'])
        for row in saved['interfaces']:
            for key in ('name', 'connection', 'managed', 'up', 'mac'):
                print(row[key])
    else:
        raise ValueError('Unknown recovery action')
