#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
exec node tools/package.cjs install "$@"
