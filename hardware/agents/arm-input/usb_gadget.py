"""Owned configfs keyboard + Switch controller. Never modifies USB network functions.
HID descriptors: Gezine/CivSwitch, MIT; see CivSwitch-MIT.txt.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
import time

GAMEPAD_DESCRIPTOR = bytes.fromhex('05 01 09 05 A1 01 15 00 25 01 35 00 45 01 75 01 95 10 05 09 19 01 29 10 81 02 05 01 25 07 46 3B 01 75 04 95 01 65 14 09 39 81 42 65 00 95 01 81 01 26 FF 00 46 FF 00 09 30 09 31 09 32 09 35 75 08 95 04 81 02 06 00 FF 09 20 95 01 81 02 0A 21 26 95 08 91 02 C0')
KEYBOARD_DESCRIPTOR = bytes.fromhex('05 01 09 06 A1 01 75 01 95 08 05 07 19 E0 29 E7 15 00 25 01 81 02 95 01 75 08 81 01 95 05 75 01 05 08 19 01 29 05 91 02 95 01 75 03 91 01 95 06 75 08 15 00 25 65 05 07 19 00 29 65 81 00 C0')
ROOT = Path('/sys/kernel/config/usb_gadget')
UDCS = Path('/sys/class/udc')
HID_CLASS = Path('/sys/class/hidg')
NAME = 'iz2s-dual'


def write(path, value):
    path.write_bytes(value if isinstance(value, bytes) else (str(value) + '\n').encode())


def read(path):
    return path.read_text().strip() if path.exists() else ''


def check_bindings(root=ROOT):
    bound = [(g, read(g / 'UDC')) for g in root.iterdir() if g.is_dir() and read(g / 'UDC')]
    for gadget, _ in bound:
        if gadget.name == NAME:
            continue
        functions = list((gadget / 'functions').iterdir())
        if not functions or any(not f.name.startswith('hid.') for f in functions):
            raise RuntimeError('USB is used by a network/storage/other gadget. Keep that setup and use a separate USB device controller for automatic input.')
    return bound


def config_paths(folder):
    value = json.loads((folder / 'input-config.json').read_text())
    for key in ('hidDevice', 'gamepadDevice'):
        path = Path(value[key])
        if path.parent != Path('/dev') or not path.name.startswith('hidg') or not path.name[4:].isdigit() or not path.is_char_device():
            raise RuntimeError('USB HID device mapping is unavailable')
    if value['hidDevice'] == value['gamepadDevice']:
        raise RuntimeError('Keyboard and controller need separate HID devices')
    return value


def start(folder):
    if os.geteuid() != 0:
        raise RuntimeError('USB setup requires root')
    subprocess.run(['modprobe', 'libcomposite'], check=True, capture_output=True)
    if not os.path.ismount('/sys/kernel/config'):
        subprocess.run(['mount', '-t', 'configfs', 'none', '/sys/kernel/config'], check=True, capture_output=True)
    if not ROOT.exists():
        raise RuntimeError('USB configfs is unavailable. Enable the board USB device/OTG driver first.')
    controllers = list(UDCS.iterdir())
    if len(controllers) != 1:
        raise RuntimeError('Automatic setup requires exactly one USB device controller; configure a dedicated controller first.')
    folder = Path(folder)
    with (folder / 'usb-gadget.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        gadget = ROOT / NAME
        if read(gadget / 'UDC'):
            try:
                return config_paths(folder)
            except (OSError, ValueError, KeyError, RuntimeError):
                pass
        bound = check_bindings()
        previous = [(str(g), controller) for g, controller in bound if g != gadget]
        backup = folder / 'usb-gadget-backup.json'
        if previous:
            backup.write_text(json.dumps(previous));backup.chmod(0o600)
        try:
            for g, _ in bound:
                write(g / 'UDC', '')
            before = {p.name for p in HID_CLASS.glob('hidg*')}
            gadget.mkdir(exist_ok=True)
            config = gadget / 'configs/c.1'
            # Recreate only our own functions; foreign HID gadgets remain intact.
            if config.exists():
                for link in config.iterdir():
                    if link.is_symlink():link.unlink()
            functions = gadget / 'functions'
            if functions.exists():
                for function in functions.iterdir():function.rmdir()
            for key, value in {'idVendor':'0x0f0d','idProduct':'0x0092','bcdUSB':'0x0200','bcdDevice':'0x0100','bDeviceClass':0,'bDeviceSubClass':0,'bDeviceProtocol':0,'bMaxPacketSize0':64}.items():
                write(gadget / key, value)
            strings = gadget / 'strings/0x409';strings.mkdir(exist_ok=True)
            for key, value in {'serialnumber':'IZ2S-ARM-DUAL','manufacturer':'IZ2S','product':'IZ2S Keyboard + Controller'}.items():write(strings / key, value)
            config.mkdir(exist_ok=True);write(config / 'MaxPower', 100);write(config / 'bmAttributes', '0x80')
            for name, descriptor, protocol in [('gamepad', GAMEPAD_DESCRIPTOR, 0), ('keyboard', KEYBOARD_DESCRIPTOR, 1)]:
                function = functions / ('hid.' + name);function.mkdir()
                write(function / 'subclass', protocol);write(function / 'protocol', protocol)
                write(function / 'report_length', 8);write(function / 'report_desc', descriptor)
                if (function / 'interval').exists():write(function / 'interval', 1)
                (config / ('hid.' + name)).symlink_to(function)
            write(gadget / 'UDC', controllers[0].name)
            deadline = time.monotonic() + 4
            while True:
                created = sorted((p.name for p in HID_CLASS.glob('hidg*') if p.name not in before), key=lambda name:int(name[4:]))
                if len(created) == 2 and all((Path('/dev') / name).is_char_device() for name in created):break
                if time.monotonic() >= deadline:raise RuntimeError('Two USB HID devices were not created')
                time.sleep(.1)
            # Kernel hidg minors are allocated at function creation, in the order above.
            value = {'hidDevice':'/dev/' + created[1], 'gamepadDevice':'/dev/' + created[0], 'usbGadget':NAME}
            temporary = folder / 'input-config.json.tmp';temporary.write_text(json.dumps(value));temporary.chmod(0o600)
            temporary.replace(folder / 'input-config.json')
            return value
        except BaseException:
            if read(gadget / 'UDC'):write(gadget / 'UDC', '')
            for old, controller in previous:write(Path(old) / 'UDC', controller)
            raise


def stop(folder):
    gadget = ROOT / NAME
    if read(gadget / 'UDC'):write(gadget / 'UDC', '')
    backup = Path(folder) / 'usb-gadget-backup.json'
    if backup.exists():
        for old, controller in json.loads(backup.read_text()):
            path = Path(old)
            if path.parent == ROOT and (path / 'UDC').exists():write(path / 'UDC', controller)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['start', 'stop'])
    parser.add_argument('--folder', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    result = start(args.folder) if args.action == 'start' else stop(args.folder)
    print(json.dumps(result))
