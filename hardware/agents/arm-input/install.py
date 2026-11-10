#!/usr/bin/env python3
"""Explicit installation of the owned input agent; never sends HID reports."""
import argparse, base64, hashlib, json, os, subprocess
from pathlib import Path

def install(folder, files, hid='/dev/hidg0', dual_hid=True):
    folder = Path(folder)
    if os.geteuid() != 0:
        raise RuntimeError('Install as root. Use sudo for manual installation.')
    if not Path('/run/systemd/system').is_dir():
        raise RuntimeError('A running systemd installation is required.')
    if not folder.is_absolute() or str(folder) in ('/', '/etc', '/usr', '/root', '/opt'):
        raise ValueError('Choose a dedicated absolute installation directory.')
    if not __import__('re').fullmatch(r'/dev/hidg\d+', hid):
        raise ValueError('Invalid HID gadget device path.')
    sums = json.loads(base64.b64decode(files['checksums.json']))
    expected = sums['files']
    if set(files) != set(expected) | {'checksums.json'}:
        raise ValueError('Agent file inventory does not match checksums.')
    decoded = {}
    for name, digest in expected.items():
        if '/' in name or '\\' in name or name in ('.', '..'):
            raise ValueError('Agent paths must be simple file names.')
        data = base64.b64decode(files[name], validate=True)
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('Agent checksum mismatch: ' + name)
        decoded[name] = data
    for active in Path('/run').glob('ns2jb-keyboard-jobs*/current.json'):
        try:
            identifier = json.loads(active.read_text()).get('requestId', '')
            if identifier and subprocess.run(['systemctl', 'is-active', '--quiet', 'ns2jb-hid-' + identifier], capture_output=True).returncode == 0:
                raise RuntimeError('Stop the current USB input job before installation.')
        except (OSError, ValueError):
            pass
    folder.mkdir(parents=True, exist_ok=True, mode=0o700)
    for name, data in decoded.items():
        target = folder / name
        if target.is_symlink():
            raise RuntimeError('Refusing to replace an agent symlink: ' + name)
        temporary = folder / (name + '.tmp')
        with temporary.open('wb') as stream:
            stream.write(data)
        temporary.chmod(0o600)
        temporary.replace(target)
    (folder / 'checksums.json').write_text(json.dumps(sums, indent=2) + '\n')
    config = folder / 'input-config.json'
    if not config.exists():
        config.write_text(json.dumps({'hidDevice': hid}))
    config.chmod(0o600)
    if dual_hid:
        import importlib.util
        spec = importlib.util.spec_from_file_location('iz2s_usb_setup', folder / 'usb_gadget.py')
        gadget = importlib.util.module_from_spec(spec);spec.loader.exec_module(gadget)
        gadget.start(folder)
        usb_command = ' '.join(json.dumps(x).replace('%', '%%') for x in ['/usr/bin/python3', str(folder / 'usb_gadget.py')])
        usb_unit = Path('/etc/systemd/system/iz2s-usb.service')
        usb_unit.write_text('[Unit]\nDescription=IZ2S USB keyboard and controller\nAfter=systemd-modules-load.service local-fs.target\nBefore=iz2s-discovery.service\n[Service]\nType=oneshot\nRemainAfterExit=yes\nExecStart=' + usb_command + ' start\nExecStop=' + usb_command + ' stop\n[Install]\nWantedBy=multi-user.target\n')
        usb_unit.chmod(0o644)
    unit = 'iz2s-discovery.service'
    quoted = ' '.join(json.dumps(x).replace('%', '%%') for x in ['/usr/bin/python3', str(folder / 'discovery.py')])
    text = '[Unit]\nDescription=IZ2S local network discovery\nAfter=network.target\n[Service]\nExecStart=' + quoted + '\nRestart=on-failure\nRestartSec=2\nNoNewPrivileges=true\nProtectSystem=strict\nProtectHome=read-only\nRestrictAddressFamilies=AF_INET AF_UNIX\n[Install]\nWantedBy=multi-user.target\n'
    unit_file = Path('/etc/systemd/system') / unit
    unit_file.write_text(text)
    unit_file.chmod(0o644)
    subprocess.run(['systemctl', 'daemon-reload'], check=True, capture_output=True, timeout=10)
    if dual_hid:
        subprocess.run(['systemctl', 'enable', 'iz2s-usb.service'], check=True, capture_output=True, timeout=10)
        subprocess.run(['systemctl', 'start', 'iz2s-usb.service'], check=True, capture_output=True, timeout=10)
    subprocess.run(['systemctl', 'enable', '--now', unit], check=True, capture_output=True, timeout=10)
    subprocess.run(['systemctl', 'restart', unit], check=True, capture_output=True, timeout=10)
    return {'installed': True, 'directory': str(folder), 'keyboard': Path(json.loads(config.read_text())['hidDevice']).exists(), 'gamepadInput': dual_hid, 'note': 'Keyboard + controller installed.' if dual_hid else 'Keyboard-only package installed.'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder', default='/opt/iz2s/arm-input')
    parser.add_argument('--hid', default='/dev/hidg0')
    parser.add_argument('--keyboard-only', action='store_true', help='Keep an existing keyboard gadget for SmileBASIC only')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    manifest = json.loads((here / 'checksums.json').read_text())
    names = list(manifest['files']) + ['checksums.json']
    files = {name: base64.b64encode((here / name).read_bytes()).decode() for name in names}
    print(json.dumps(install(args.folder, files, args.hid, dual_hid=not args.keyboard_only)))
