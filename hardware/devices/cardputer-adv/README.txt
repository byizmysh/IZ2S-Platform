IZ2S Platform - Cardputer ADV input
Firmware application: 0.21.1 (M5Launcher file IZ2S-ADV-0.21.bin)

Select Cardputer ADV as the input device in the panel. ARM PC is not required.
The PC preloads the text/actions onto ADV SD; later Wi-Fi commands choose an
action, and ADV types locally over USB as the Switch keyboard.

FIRST SETUP
1. Connect ADV to the PC and choose USB storage in M5Launcher.
2. On Linux, use direct USB copy only when the panel positively identifies its
   card. On Windows/macOS, or when discovery is unavailable, choose File and
   extract the downloaded ZIP into the root of the ADV card. It adds:
     Utilities/IZ2S-ADV-0.21.bin
     IZ2S/input.json
   The pairing key stays private to this PC and ADV. Preserve existing Wi-Fi
   configuration; do not replace it with an empty template. Do not remove the
   other firmware. The image is an application, not a bootloader/address-zero
   flash image; M5Launcher must remain installed.
3. Eject the storage normally, exit USB storage and select
   SD -> Utilities -> IZ2S-ADV-0.21.bin in M5Launcher to install/run the app.
4. Choose the local Wi-Fi that can reach the PC. Keys 1-5 select an access point,
   N/P change pages and M enters an SSID manually. Type the password on the ADV
   keyboard and press Enter. Tab shows/hides it; it starts hidden. Keyboard
   controls are displayed in the footer. A failed connection returns to Wi-Fi
   selection; holding G0 for two seconds while idle reopens setup.
5. If iz2s-adv.local does not resolve, enter the IP shown on the ADV screen in
   Device settings. A plain IP is accepted. Guest Wi-Fi/client isolation or a
   firewall can block this even when ADV shows connected.
6. Move the USB DATA cable from the PC to Switch 2 and confirm that in IZ2S.
   Complete the source entry/save stage, fully restart Switch 2, open fresh
   SmileBASIC Direct and follow Launch. The six panel stages are firmware,
   Wi-Fi, USB to Switch, entry/save, console restart and Launch.

KEYBOARD AND RECOVERY
Use the expected SmileBASIC English/US layout. Set Switch keyboard Caps Lock
state in the panel to match the console. ADV compensates for that state while
preserving source strings; do not blindly uppercase every quoted string.
Firmware 0.21.1 uses an unnumbered eight-byte USB boot keyboard report and waits
for USB report completion. Status includes usbKeyboardFormat, hidProtocol,
reportsSent and reportErrors. Wi-Fi alone does not prove USB keyboard readiness.

Panel Stop or a short G0 press stops input. Lost Wi-Fi/USB or loss of server
polling stops input; the polling timeout is 15 seconds. Accepted jobs are not
replayed on duplicate requests, and interrupted jobs do not resume after reboot.
After partial input, restore the correct SmileBASIC screen before starting a
new job. After failed/ended native execution, fully restart Switch 2.

The current IZ2S Core saves NS2IZM; the bundled alternative saves NS2B. Upgrading
from the old 0.16 NS2I bootstrap requires First Setup again, not just a rename.

BUILD AND HOST STATUS
Source: hardware/agents/cardputer-adv/; versions pinned in platformio.ini.
Build with Python 3 and PlatformIO 6.2.0 using build.py. That script builds an
M5Launcher application image; it does not flash a device. Firmware manifests
include source/image hashes. Preserve the library notices in NOTICE.txt.
The ARM input workflow remains separate and compatible with input API 1.

Windows/macOS installers and manual SD copying require real OS testing. Native
session declarations currently remain Linux-only. Read COMPATIBILITY.md and
FAQ.md in the platform root. A physical Linux x64 / ADV / IZPSlauncher path has
reached the running launcher; this does not qualify every host/device/module.

Device documentation: https://docs.m5stack.com/en/core/Cardputer-Adv
M5Launcher application format:
https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project
Library notices: ../../agents/cardputer-adv/NOTICE.txt
