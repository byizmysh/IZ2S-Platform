# Civilization VII LDN integration in IZ2S 0.3.2

Originally copied from ItsNerisma/CivSwitchLDN revision `91668edc8141f6764294fd969bf8373b625626ba` on 2026-11-08. The original CLI remains as upstream material; the panel uses its own adapter, session, runner, setup and network wrapper.

The current user route is [IZ2SCiv core](../iz2sciv-core/README.md), with four guided steps. Initial LDN delivery requires native Linux x64 / ARM64 and direct access to a compatible Wi-Fi radio. Ethernet alone and WSL are not supported for this step. Target: Switch 2 firmware 23.0.1 and the Nintendo Switch 1 cartridge of Civilization VII 1.0.0. The current procedure requires English interface and USB keyboard input settings before starting the game.

The PC preparation step checks isolated Python/Node dependencies and the owner's Switch 1 keys. Keys are stored privately under `data/private/civswitchldn/`; values must not appear in logs, source snapshots or archives. A dedicated interpreter requires network capabilities; the panel shows the one-time permission command and validates readiness rather than silently granting it.

The panel wrapper releases the selected Wi-Fi radio for delivery and restores its previous state. Recovery records are written before release and retained on recovery failure. Other radios and Ethernet are outside this operation. Stop waits for cleanup; after an interrupted process or failed restoration, use the panel's Restore PC Wi-Fi action before another transfer.

Delivery and network restoration must finish before the panel checks the game loader on TCP 9026. Safe probes can retry; another interrupted delivery requires a freshly confirmed lobby. A successful reconnect automatically installs IZ2SCIV.Civ7Save through the current IzCivSwitchFS installer, which replaces only its trigger and confirms commit. The upstream installer retained in this directory has different behavior and is not the panel's current guided save writer.

The original GPL-3.0-only license, MIT notices for Gezine's files and GPLv3 LDN notices remain in LICENSE, LICENSES and THIRD_PARTY_NOTICES.md. This integration note does not replace them. See the root SOURCES.md for the overall source scope and outstanding native build limitations.
