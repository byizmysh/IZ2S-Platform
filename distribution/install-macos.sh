#!/bin/sh
# Uses the tools included with macOS; no Homebrew, Xcode or system Python needed.
set -eu
IZ2S_SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
[ "$(uname -s)" = Darwin ] || { echo 'This entry point requires macOS.' >&2; exit 1; }
IZ2S_OS_VERSION=$(/usr/bin/sw_vers -productVersion)
if ! printf '%s\n' "$IZ2S_OS_VERSION" | /usr/bin/awk -F. '{exit !($1>13 || ($1==13 && $2>=5))}'; then
  echo 'The private Node.js 24 runtime requires macOS 13.5 or later.' >&2; exit 1
fi
case "$(uname -m)" in arm64) IZ2S_ARCH=arm64;; x86_64) IZ2S_ARCH=x64;; *) echo 'An Intel or Apple silicon Mac is required.' >&2; exit 1;; esac
# Detect native Apple silicon even when Terminal is running under Rosetta.
if [ "$(/usr/sbin/sysctl -in hw.optional.arm64 2>/dev/null || true)" = 1 ]; then IZ2S_ARCH=arm64; fi
IZ2S_BOOTSTRAP="$HOME/Library/Application Support/IZ2S Platform/bootstrap/node-$IZ2S_ARCH"
if [ ! -x "$IZ2S_BOOTSTRAP/bin/node" ]; then
  echo 'Downloading a private Node.js 24 runtime and verifying its SHA-256...'
  IZ2S_TEMP=$(mktemp -d "${TMPDIR:-/tmp}/iz2s-node.XXXXXX")
  trap 'rm -rf -- "$IZ2S_TEMP"' EXIT HUP INT TERM
  IZ2S_BASE=https://nodejs.org/dist/latest-v24.x
  /usr/bin/curl --fail --location --proto '=https' --tlsv1.2 --connect-timeout 15 "$IZ2S_BASE/SHASUMS256.txt" -o "$IZ2S_TEMP/SHASUMS256.txt"
  IZ2S_ARCHIVE=$(/usr/bin/awk -v arch="$IZ2S_ARCH" '$2 ~ ("^node-v24\\.[0-9]+\\.[0-9]+-darwin-" arch "\\.tar\\.gz$") {print $2}' "$IZ2S_TEMP/SHASUMS256.txt")
  case "$IZ2S_ARCHIVE" in ''|*'
'*) echo 'Could not resolve the official Node.js archive.' >&2; exit 1;; esac
  /usr/bin/curl --fail --location --proto '=https' --tlsv1.2 --connect-timeout 15 "$IZ2S_BASE/$IZ2S_ARCHIVE" -o "$IZ2S_TEMP/$IZ2S_ARCHIVE"
  (cd "$IZ2S_TEMP" && /usr/bin/awk -v name="$IZ2S_ARCHIVE" '$2==name' SHASUMS256.txt | /usr/bin/shasum -a 256 -c -)
  mkdir -p "$IZ2S_TEMP/unpacked" "$(dirname -- "$IZ2S_BOOTSTRAP")"
  /usr/bin/tar -xzf "$IZ2S_TEMP/$IZ2S_ARCHIVE" --strip-components=1 -C "$IZ2S_TEMP/unpacked"
  [ ! -e "$IZ2S_BOOTSTRAP" ] || { echo 'Incomplete Node bootstrap already exists; remove that bootstrap folder and retry.' >&2; exit 1; }
  mv -- "$IZ2S_TEMP/unpacked" "$IZ2S_BOOTSTRAP"
  rm -rf -- "$IZ2S_TEMP"
  trap - EXIT HUP INT TERM
fi
"$IZ2S_BOOTSTRAP/bin/node" -e 'if(Number(process.versions.node.split(String.fromCharCode(46))[0])<24)process.exit(1)'
"$IZ2S_BOOTSTRAP/bin/node" "$IZ2S_SOURCE/distribution/install.cjs" --node-home "$IZ2S_BOOTSTRAP" "$@"
