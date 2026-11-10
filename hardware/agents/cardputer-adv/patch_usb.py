"""Verify the pinned USB ABI; the dual HID driver is built in this project only."""
from pathlib import Path
import hashlib, json
Import('env')
from pathlib import Path
env.Append(CCFLAGS=['-ffile-prefix-map=' + str(Path.home()) + '=.', '-fmacro-prefix-map=' + str(Path.home()) + '=.'])
framework=Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32'))
if not json.loads((framework/'package.json').read_text())['version'].startswith('3.20017.'):
    raise RuntimeError('IZ2S dual HID requires pinned Arduino-ESP32 2.0.17')
header=framework/'tools/sdk/esp32s3/include/arduino_tinyusb/tinyusb/src/class/hid/hid_device.h'
if hashlib.sha256(header.read_bytes()).hexdigest()!='b76a2138062f0f45a03a6c6def8de3274444db8dc85f523fd7be20101180637c':
    raise RuntimeError('TinyUSB HID ABI differs from verified 0.16.0')
print('IZ2S ADV: independent Switch gamepad + eight-byte boot keyboard interfaces')
