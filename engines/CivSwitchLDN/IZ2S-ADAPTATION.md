# IZ2S RUN22 integration

Copied from ItsNerisma/CivSwitchLDN revision
`91668edc8141f6764294fd969bf8373b625626ba` on 2026-11-08.
The original `run.js` CLI and its network wrapper remain available. The panel
uses separate `adapter.cjs`, `session.cjs`, `panel-runner.cjs`, `setup.cjs`,
`integration.cjs`, and `bash/network-panel.sh`.

Select Civilization VII, then **CIV7 LAN · LDN**. This is a Core only engine
on Linux x64 and ARM64. Ethernet alone cannot deliver the initial payload:
LDN requires direct access to a Linux Wi-Fi radio. ADV, USB keyboard input,
CHAT, controller A, and English keyboard settings are unnecessary in this branch.
Use the Switch 1 cartridge, game version 1.0.0, and a fresh wireless host lobby.

Step 1 prepares Node/Python dependencies without running the upstream privileged
postinstall. Python is isolated in `data/runtime/civswitchldn`. A dedicated copied
interpreter needs CAP_NET_ADMIN and CAP_NET_RAW; the UI shows the exact one-time
command, but does not silently grant these permissions. This interpreter can
administer network devices, so access to it should remain restricted to the PC
owner. The panel validates readiness before starting a delivery.

The user must select their own `prod.keys` from their Switch 1 in step 1. Required
LDN key entries are checked without reporting their values. Keys are stored in
`data/private/civswitchldn/prod.keys`, with directory mode 0700 and file mode 0600.
No key contents are included in snapshots, setup reports, logs or release source.
No existing keys elsewhere on the PC are imported automatically.

The panel network wrapper releases only interfaces on the selected Wi-Fi radio.
It records their managed state, connection UUID and link state, and restores
them in EXIT/INT/TERM cleanup. Ethernet, VPNs and other radios remain untouched.
STOP waits for the delivery process and its network restoration to close; it
does not replay messages. A force-kill, PC shutdown or crash can interrupt this
cleanup; reconnect that Wi-Fi radio manually before another attempt.

The payload compressor currently produces 19 messages. Delivery and restoration
must both confirm completion before the panel probes the Remote JS Loader on
TCP 9026. Only harmless probes retry automatically. A failed or interrupted
delivery needs an explicitly confirmed fresh lobby before another send.

Save installation is optional and requires a live verified loader at the same
IP plus explicit consent to delete ALL single-player saves. The upstream
installer has one local adaptation: its trigger is named **IZ2S.Civ7Save**.
After INSTALL DONE, restart the game and choose Load Game. The panel does not
claim IZlauncher/IZPSlauncher support; those currently require SmileBASIC 4.

The original GPL-3.0-only license, MIT notices for Gezine's files and GPLv3 LDN
dependency notices are retained. See LICENSE, LICENSES and THIRD_PARTY_NOTICES.md.
Any distributed bundle containing installed dependencies must include their
corresponding source and notices as required by their licenses; local runtime
directories are not release sources.
