#!/bin/sh
set -eu
IZ2S_SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
iz2s_finish() {
  IZ2S_RESULT=$?
  trap - EXIT
  if [ "$IZ2S_RESULT" -ne 0 ]; then echo 'Setup failed. Read the message above and FAQ.md.' >&2; fi
  if [ -t 0 ]; then printf '\nPress Enter to close this installer. '; read -r IZ2S_DONE || :; fi
  exit "$IZ2S_RESULT"
}
trap iz2s_finish EXIT
case "$(uname -s)" in
  Darwin) /bin/sh "$IZ2S_SOURCE/distribution/install-macos.sh" "$@";;
  Linux) /bin/sh "$IZ2S_SOURCE/install.sh" --launch "$@";;
  *) echo 'Open Install.cmd on Windows.' >&2; exit 1;;
esac
