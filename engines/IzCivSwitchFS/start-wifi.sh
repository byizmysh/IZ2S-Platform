#!/usr/bin/env bash
set -euo pipefail
# The old developer-only LDN wrapper depended on an unshipped .local-setup
# directory and a personal key path. Use the packaged panel preparation flow.
printf '%s\n' 'Initial Civilization VII wireless setup is provided by the IZ2S panel.' 'Open the panel, complete PC preparation, then use Connect and install save.'
if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then exit 0; fi
exit 2
