# LDN client

This directory handles the local wireless part of CivSwitchLDN. `run.js` calls `main.py` to scan nearby LDN networks, connect to the selected host, join its Civilization VII lobby and send the compressed exploit through game chat.

The client scans LDN networks, connects to the selected host, joins the game lobby and reads chat messages from stdin. A single scan covers all configured channels; it retries up to two more times only if no network is found (or the requested `--host` is missing). Node selects the radio and manages network restoration. Scan timing and lobby settings are constants in the Python modules. Output shows functional steps by default; `npm start -- --verbose` also enables packet traces and diagnostics.

The scan does not filter networks by game identifier. If several LDN networks are found, use `--host <mac>` to select the Civilization VII host.

The code is split by responsibility:

- `main.py` checks the environment and starts the CLI.
- `cli.py` accepts the radio, key file, interface name and optional host MAC from Node.
- `session.py` discovers the host and keeps the LDN connection open during delivery.
- `lobby.py` performs the game handshake and schedules chat messages.
- `transport.py` handles ordered UDP packets and retransmissions.
- `protocol.py` builds and identifies Civilization VII messages.
- `connection.py` loads keys and configures the LDN library.

This client is part of CivSwitchLDN and is licensed under
[GNU GPL version 3 only](../LICENSE). It imports Kinnay's GPLv3
[`ldn==0.0.21`](https://github.com/kinnay/LDN) package, installed separately
through `requirements.txt`. The missing-host-announcement fallback in
`connection.py` is application-side behavior. See
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) for attribution and
redistribution details.
