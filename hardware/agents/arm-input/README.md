# IZ2S ARM Input · 1.0.0

## Host and device compatibility

The input board remains Linux regardless of the PC server OS. Windows/macOS PC installers are experimental; full native console sessions are currently declared for Linux only. See the platform compatibility matrix before testing another host. A PC installer can prepare its own SSH client; it cannot provide board firmware, SSH trust or a USB gadget driver.

## Before you install

Install this package on the ARM board, not on Switch and not as the PC server. A normal PC application installation only supplies the files; board deployment is a separate explicit action. The board must already have Linux, SSH and a working USB keyboard gadget. Installation can configure this package and its discovery service, but does not repair every board OS or enable its USB driver.

[Main README](../../../README.md) · [Plain-English FAQ](../../../FAQ.md)

This is the complete deployable input-device module shipped with IZ2S Platform. It is separate from the PC server and console shell. Source and installer: `hardware/agents/arm-input/`. The panel supplies a ready `iz2s-arm-input.tar.gz` with all files and SHA-256 inventory. No repository checkout or Node.js installation is required on ARM.

## 1. Prepare the board

Use Linux with Python 3, running systemd, SSH and a USB device/OTG controller (UDC). A USB-C socket alone does not imply device-mode support. Connect the board's data/OTG socket to Switch and connect both PC and board to the same LAN. A dedicated root-capable input appliance is required: writing HID reports and creating transient systemd jobs need root. Non-root SSH login cannot silently elevate.

The board OS must already expose a keyboard gadget, normally `/dev/hidg0`. Check `ls /sys/class/udc` and `ls /dev/hidg*` on the board. If absent, follow the board vendor's USB gadget setup for that exact OS. This package does not modify boot settings, replace drivers or overwrite an existing USB gadget. Installation can succeed before the USB cable is attached, but the launcher remains blocked until the keyboard link is configured.

## 2A. Install from the panel over SSH

Enter the ARM IP, SSH user and optionally a private-key path stored on this PC. The installer uses standard OpenSSH with strict host-key checking. It does not save passwords or upload private keys. Root is the default user; another account must already have equivalent privileges.

For the first SSH connection, use a terminal on the PC:

```sh
ssh root@ARM_IP
# Verify the fingerprint against the board's own console, then exit.
ssh-keygen -t ed25519
ssh-copy-id root@ARM_IP
```

Only create a key if you do not already have one. `ssh-copy-id` may ask for the board password in the terminal. Never paste it into panel settings. Existing keys and an SSH agent also work. The panel's noninteractive installer requires key authentication; password-only users can follow the manual route below.

Choose **Install over SSH** in First-time setup. The panel sends this exact package, verifies every source hash remotely, installs it in `/opt/iz2s/arm-input` (or your configured dedicated folder), writes input-config.json and enables `iz2s-discovery.service`. No console keyboard commands are sent during installation. **Check connection** verifies the installed version and USB state separately.

## 2B. Manual or password-only installation

Download the package from the panel, or use `packages/iz2s-arm-input.tar.gz` inside the platform folder. On the PC:

```sh
scp packages/iz2s-arm-input.tar.gz root@ARM_IP:/tmp/
ssh root@ARM_IP
```

On the ARM board:

```sh
mkdir -p /tmp/iz2s-input-setup
tar -xzf /tmp/iz2s-arm-input.tar.gz -C /tmp/iz2s-input-setup
cd /tmp/iz2s-input-setup/arm-input
sudo python3 install.py --folder /opt/iz2s/arm-input --hid /dev/hidg0
```

Copy by USB storage instead of scp for offline installation. Archive paths are relative, and the installer validates its complete file/hash inventory. Downloads need no network at install time. Keep the panel's target folder consistent with the manually installed folder.

## 3. Verify and operate

Return to the panel, save the ARM address/settings and choose Check connection. Discovery advertises only device identity, addresses and capabilities over LAN UDP 39418; it accepts no HID commands. SSH remains the authenticated control channel. USB readiness additionally requires the Switch-side link. Neither a successful SSH login nor a successful installation proves the USB cable is usable.

The agent runs only explicitly submitted input jobs as owned transient systemd units. It checks the keyboard LED response before typing a plan, releases keys on completion/cancellation, and refuses replacement while an input job is active. Routine panel checks are read only and never install/update files. Updates use the same explicit installer; do not overwrite an active job.

## Remove

Stop the launcher session first. On the board, run `sudo systemctl disable --now iz2s-discovery.service`, remove `/etc/systemd/system/iz2s-discovery.service`, reload systemd and remove only the dedicated installation folder. This package does not own the board's SSH service, system drivers or USB boot configuration; leave them intact.

## SSH connection refused

If ping works but SSH reports Connection refused, the network address is reachable but no SSH connection can be established on that port. A password or private-key change does not fix this pre-authentication failure. Start the existing SSH service locally on the board: `sudo systemctl enable --now ssh` (or sshd on distributions using that service name). If no terminal is available, obtain a local console or another already configured management channel first.

The keyboard-only USB gadget does not provide an administrative shell or network adapter. Seeing NS2 Bootstrap Keyboard on a PC proves USB enumeration there, not successful typing on Switch. A composite serial/network gadget is a separate board/OS setup; this installer does not replace a working keyboard gadget automatically.
