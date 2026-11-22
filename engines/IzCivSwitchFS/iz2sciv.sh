#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec "$task_root/../../tools/node" "$task_root/tools/launch.mjs" "$@"
