#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ $# == 1 && "$1" != -* ]]; then
    exec bash "$task_root/iz2sciv.sh" install --host "$1"
fi
exec bash "$task_root/iz2sciv.sh" install "$@"
