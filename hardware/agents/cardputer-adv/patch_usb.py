"""Patch the pinned Arduino 2.0.17 keyboard for a single eight-byte boot report.

The patch is applied only to PlatformIO's selected framework. No other device
interfaces are used by this application. Unsupported framework sources fail closed.
"""
from pathlib import Path
import json
Import('env')
framework=Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32'))
metadata=json.loads((framework/'package.json').read_text())
if not metadata['version'].startswith('3.20017.'):
    raise RuntimeError('IZ2S ADV USB patch requires pinned Arduino-ESP32 2.0.17')
usb=framework/'libraries/USB/src/USBHID.cpp'
keyboard=framework/'libraries/USB/src/USBHIDKeyboard.cpp'
text=usb.read_text()
old='TUD_HID_INOUT_DESCRIPTOR(*itf, str_index, HID_ITF_PROTOCOL_NONE,'
new='TUD_HID_INOUT_DESCRIPTOR(*itf, str_index, HID_ITF_PROTOCOL_KEYBOARD,'
if old in text:text=text.replace(old,new,1)
elif new not in text:raise RuntimeError('Pinned USB interface descriptor changed')
usb.write_text(text)
text=keyboard.read_text()
marker='// IZ2S single-report boot keyboard compatibility.'
if marker not in text:
    expected='    hid.SendReport(HID_REPORT_ID_KEYBOARD, &report, sizeof(report));'
    if expected not in text:raise RuntimeError('Pinned keyboard sender changed')
    text=text.replace('#include "USBHIDKeyboard.h"','#include "USBHIDKeyboard.h"\n\n'+marker+'\nbool iz2s_hid_last_report_ok=true;\nuint32_t iz2s_hid_reports_sent=0,iz2s_hid_report_errors=0;',1)
    text=text.replace('TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_REPORT_ID_KEYBOARD))','TUD_HID_REPORT_DESC_KEYBOARD()')
    text=text.replace('if(report_id == HID_REPORT_ID_KEYBOARD){','if(report_id == 0 || report_id == HID_REPORT_ID_KEYBOARD){')
    text=text.replace(expected,'    iz2s_hid_last_report_ok=hid.SendReport(0, &report, sizeof(report));\n    if(iz2s_hid_last_report_ok)iz2s_hid_reports_sent++;else iz2s_hid_report_errors++;')
    keyboard.write_text(text)
for expected in ['TUD_HID_REPORT_DESC_KEYBOARD()', 'hid.SendReport(0, &report, sizeof(report))',marker]:
    if expected not in keyboard.read_text():raise RuntimeError('Boot keyboard patch was not applied completely')
print('IZ2S ADV: boot keyboard, no Report ID, acknowledged eight-byte reports')
