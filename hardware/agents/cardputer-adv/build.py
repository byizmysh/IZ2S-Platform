#!/usr/bin/env python3
"""Build the M5Launcher application image, keeping downloads/build output outside IZ2S."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
OUTPUT = HERE.parents[1] / 'devices/cardputer-adv/firmware'
pio = os.environ.get('IZ2S_PLATFORMIO') or shutil.which('pio')
if not pio:
    raise SystemExit('Install PlatformIO 6.2.0, or set IZ2S_PLATFORMIO to the pio executable.')
subprocess.run([pio, 'run', '-d', str(HERE)], check=True)
image = Path('/tmp/iz2s-release-cardputer-build/cardputer-adv/firmware.bin').read_bytes()
if image[0] != 0xE9 or len(image) > 3 * 1024 * 1024:
    raise SystemExit('Unexpected application image')
OUTPUT.mkdir(parents=True, exist_ok=True)
filename = 'IZ2S-ADV.bin'
(OUTPUT / filename).write_bytes(image)
(OUTPUT / 'IZ2S-ADV-CIV.bin').write_bytes(image)
manifest = {
    'device': 'M5Stack Cardputer ADV', 'version': '0.22.4', 'file': filename,
    'type': 'esp32s3-application', 'inputApiVersion': 2,
    'textStorage': 'SD content package; PC selects named action and raw source line', 'inputModes': ['basic', 'raw-text', 'raw-lines', 'gamepad'], 'usb': 'native-otg-dual-hid-keyboard-switch-gamepad',
    'usbInterfaces': 2, 'gamepadFormat': 'switch-hori-8-byte',
    'install': 'M5Launcher SD application', 'bytes': len(image),
    'sha256': hashlib.sha256(image).hexdigest(),
    'platformio': '6.2.0', 'platform': 'espressif32@6.9.0', 'arduinoEsp32': '2.0.17',
    'sources': {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                for name in ['platformio.ini', 'patch_usb.py', 'src/main.cpp', 'src/screen.h', 'src/usb_recovery.h', 'src/dual_hid.h', 'src/switch_reports.h', 'src/tinyusb_hid_dual.c','src/raw_lines.h','src/contact_policy.h']},
}
(OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(OUTPUT / 'manifest-civ.json').write_text(json.dumps({**manifest,'profile':'civswitch','file':'IZ2S-ADV-CIV.bin','batchRawLines':True},indent=2)+'\n')
print('M5Launcher application: ' + str(OUTPUT / filename))
print('SHA-256: ' + manifest['sha256'])
