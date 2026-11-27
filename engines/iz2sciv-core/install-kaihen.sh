#!/usr/bin/env bash
# Compatibility filename: this fork installs IZ2S only.
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$task_root/install-iz2s.sh" "$@"
