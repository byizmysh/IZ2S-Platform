#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
exec "${NODE:-node}" desktop/install.cjs "$@"
