#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
# Keep the PC server alive after the desktop launcher or terminal exits.
exec ./tools/node distribution/start-local.cjs "$@"
