# IZ2S ARM Input · 1.1.1

One Linux input agent serves SmileBASIC 4 and Civilization VII. It exposes a USB
keyboard and a Switch-compatible controller. The PC supplies the plan; the ARM
board types the selected line and presses A automatically. No manual A mode is
exposed in the panel.

## Requirements

- Linux, Python 3, systemd, root access and SSH with a trusted host key.
- A working USB device/OTG controller, configfs and the kernel libcomposite/HID
  gadget driver. A USB-C socket alone does not guarantee device-mode support.
- LAN between PC and ARM. Use the board's USB data/OTG port for the Switch dock.

The package runs on the input board, not on Switch. No Node.js server is needed
on ARM. The installer cannot provide missing board firmware or enable an
unsupported USB device controller.

## Install from the panel

Select ARM PC, open Device settings and enter its IPv4 address, root SSH account
and optional private-key path. Verify the host fingerprint through the board's
own console first. To enable existing SSH key access from the PC:

```sh
ssh root@ARM_IP
ssh-copy-id root@ARM_IP
```

Keep passwords in the terminal. The panel uses strict host-key checking and
never uploads a private key. Confirm the installation target and choose Install
package. Installation deploys the checksum-verified package, prepares the USB
keyboard/controller and enables iz2s-usb.service and iz2s-discovery.service.
It sends no button presses or keyboard commands.

The composite gadget uses the same separate HORI gamepad and boot-keyboard HID
reports as ADV. Configfs hidg device numbers are discovered during binding;
input-config.json records their roles. Existing HID-only gadget definitions are
preserved and their previous binding is restored if setup fails. USB gadgets
with network, storage or other functions are refused before any unbinding.
Use a separate/dedicated USB controller for those boards. Keep OS-specific USB
autostart scripts from rebinding the same controller after iz2s-usb.service.

## Manual or password-only installation

Download the ARM installer from Device settings or Civ7 USB step 2. Copy
iz2s-civ7-arm-input.run (Civ7) or iz2s-arm-input.run (SB4) onto the ARM device:

```sh
scp iz2s-civ7-arm-input.run USER@ARM_IP:/tmp/
ssh USER@ARM_IP
sudo python3 /tmp/iz2s-civ7-arm-input.run
```

Both names contain the same verified keyboard + controller agent. The installer
needs only Python 3 and works without downloading anything on ARM. It verifies
all embedded files before installation. `--check` checks the board without
installing; `--check-package` checks only the embedded files on any PC. Do not
run the installer on the server PC unless it is itself the USB input board.
The ordinary iz2s-arm-input.tar.gz package with install.py remains available
for existing deployment scripts and SSH installation.

Use the same target folder in the panel. Offline copying by USB storage also
works. If the board only supports a pre-existing keyboard setup, --keyboard-only
keeps it for SmileBASIC; CivSwitch automatic input remains unavailable until
both HID interfaces are ready. The panel does not fall back to manual A.

## Connect and run

Connect ARM USB data/OTG to Switch and check the connection. The panel requires
keyboard + controller before CivSwitch input. Plans use distinct job IDs and
owned transient systemd services. A lost receipt/restart never replays a line.
STOP releases both interfaces. Controller neutral reports continue while the
keyboard types. The keyboard LED response is checked before text entry.

CivSwitch: supported cartridge version 1.0.0, English (US) system/keyboard,
MULTIPLAYER > WIRELESS > CREATE GAME > HOST LOBBY > CHAT. Confirm a fresh chat,
then Start input. The same 87-line plan is used for ARM and ADV, including
first/last-line controller timing, loader verification and optional save setup.
Wireless may disconnect Switch IP networking during input; PC-to-ARM LAN and
ARM-to-Switch USB are separate links.

## Troubleshooting and removal

SSH connection refused means no SSH service accepts that connection. Start the
board's existing ssh/sshd service locally; changing keys does not solve it.
No USB controller/configfs means the board OS needs its vendor-specific gadget
setup before installation. A successful SSH connection does not prove USB input.

After stopping input, disable iz2s-discovery.service, then stop/disable
iz2s-usb.service. Its stop action restores the saved previous HID binding.
Remove only those two service files and the dedicated installation folder,
then run systemctl daemon-reload. Leave system SSH, drivers and foreign gadget
definitions intact. Discovery UDP 39418 carries public metadata only; SSH is
the authenticated command channel.

Protocol sources: [Linux HID gadget](https://docs.kernel.org/usb/gadget_hid.html),
[configfs](https://docs.kernel.org/usb/gadget_configfs.html),
[original CivSwitch HID descriptors](https://github.com/Gezine/CivSwitch).
Physical ARM/Switch recognition still requires a hardware test.

CivSwitch batches
-----------------
The complete 87-line source and keyboard/controller sequence are transferred
before enqueueing one owned service job. Each committed line is reported only
after its submission and separator actions complete. Losing SSH does not stop
the local service. Never enqueue the same source again to recover a lost
receipt; follow the existing job identity instead. Console focus and accepted
characters cannot be verified from HID output alone.
