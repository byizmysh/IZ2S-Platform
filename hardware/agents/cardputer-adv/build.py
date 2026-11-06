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
image = Path('/tmp/iz21-cardputer-build/cardputer-adv/firmware.bin').read_bytes()
if image[0] != 0xE9 or len(image) > 3 * 1024 * 1024:
    raise SystemExit('Unexpected application image')
OUTPUT.mkdir(parents=True, exist_ok=True)
filename = 'IZ2S-ADV-0.21.bin'
(OUTPUT / filename).write_bytes(image)
manifest = {
    'device': 'M5Stack Cardputer ADV', 'version': '0.21.3', 'file': filename,
    'type': 'esp32s3-application', 'inputApiVersion': 2,
    'textStorage': 'SD content package; execution by named action', 'usb': 'native-otg-boot-keyboard-8-byte',
    'install': 'M5Launcher SD application', 'bytes': len(image),
    'sha256': hashlib.sha256(image).hexdigest(),
    'platformio': '6.2.0', 'platform': 'espressif32@6.9.0', 'arduinoEsp32': '2.0.17',
    'sources': {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                for name in ['platformio.ini', 'patch_usb.py', 'src/main.cpp', 'src/screen.h', 'src/usb_recovery.h']},
}
(OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('M5Launcher application: ' + str(OUTPUT / filename))
print('SHA-256: ' + manifest['sha256'])
