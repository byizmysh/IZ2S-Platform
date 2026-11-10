#!/usr/bin/env bash
# Post-install: create Python venv, install LDN deps, grant network capabilities.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
venv="$root/ldn/.venv"
python_ldn="$venv/bin/python-ldn"

# Python venv + deps
if [[ ! -d "$venv" ]]; then
    echo "[setup] Creating Python venv in ldn/.venv"
    python3 -m venv "$venv"
fi
echo "[setup] Installing Python dependencies"
"$venv/bin/pip" install -q -r "$root/ldn/requirements.txt"

# Dedicated Python binary with network capabilities (Linux only)
if [[ "$(uname -s)" != Linux ]]; then
    echo "[setup] Skipping capabilities (not Linux)."
    exit 0
fi

for tool in getcap setcap; do
    if ! command -v "$tool" >/dev/null; then
        echo "[setup] $tool not found. Install libcap2-bin (Debian/Ubuntu) and re-run."
        exit 1
    fi
done

source_python="$(readlink -f -- "$venv/bin/python")"
if [[ ! -f "$python_ldn" ]] || ! cmp -s -- "$source_python" "$python_ldn"; then
    install -m 0755 -- "$source_python" "$python_ldn"
fi

if sudo setcap cap_net_admin,cap_net_raw=ep "$python_ldn" 2>/dev/null; then
    echo "[setup] Capabilities granted to $python_ldn"
    getcap "$python_ldn"
else
    echo "[setup] Could not set capabilities (needs sudo). Run manually:"
    echo "  sudo setcap cap_net_admin,cap_net_raw=ep $python_ldn"
fi
