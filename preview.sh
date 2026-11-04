#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
exec ./tools/node start.cjs --preview "$@"
