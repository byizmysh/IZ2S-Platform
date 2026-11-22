#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
task_ldn="$task_root/.local-setup/CivSwitchLDN"
task_keys='/home/k2/Antigravity/MyLoveSwitch/prod.keys'
cd "$task_ldn"
if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    printf '%s\n' 'First entry: Civ VII → MULTIPLAYER → WIRELESS → CREATE GAME → HOST LOBBY.' 'Before starting, configure the network using the local instructions. NetworkManager will temporarily disconnect the network connections.'
    exec node run.js --help
fi
ldn/.venv/bin/python - "$task_keys" <<'PY'
import sys
sys.path.insert(0, 'ldn')
from connection import load_keys
load_keys(sys.argv[1])
print('LDN keys verified; their values are not displayed.')
PY
task_capabilities="$(getcap ldn/.venv/bin/python-ldn)"
if [[ "$task_capabilities" != *cap_net_admin* || "$task_capabilities" != *cap_net_raw* ]]; then
    printf '%s\n' 'The dedicated LDN interpreter needs network permissions. sudo will ask for your local password.'
    sudo setcap cap_net_admin,cap_net_raw=ep "$task_ldn/ldn/.venv/bin/python-ldn"
fi
printf '%s\n' 'Starting CivSwitchLDN. The PC network will disconnect temporarily and reconnect after the transfer.' 'Wait for Awaiting payload on the console. For the full sequence, use ./iz2sciv.sh setup: the IP address will be detected automatically, then IZ2S will be installed.'
exec npm start -- --keys "$task_keys" --phy phy0 --no-persist --defer-loader-check "$@"
