#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
if [ ! -x ./data/runtime/node/bin/node ] && ! command -v node >/dev/null 2>&1; then
  echo 'Open Install.command first to prepare the private runtime.' >&2
  if [ -t 0 ]; then printf 'Press Enter to close. '; read -r IZ2S_DONE; fi
  exit 1
fi
exec ./tools/node distribution/start-local.cjs "$@"
