#!/bin/sh
# Prepare a complete per-user installation; never starts console input.
set -eu
IZ2S_SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IZ2S_CHECK=0
IZ2S_SYSTEM=1
IZ2S_DESKTOP=1
IZ2S_LAUNCH=0
IZ2S_DEST=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --check) IZ2S_CHECK=1;;
    --no-system-packages) IZ2S_SYSTEM=0;;
    --no-desktop) IZ2S_DESKTOP=0;;
    --launch) IZ2S_LAUNCH=1;;
    --prefix) shift; IZ2S_DEST=${1:?Missing installation folder};;
    --help) echo 'Usage: ./install.sh [--check] [--prefix PATH] [--no-system-packages] [--no-desktop] [--launch]'; exit 0;;
    *) echo "Unknown option: $1" >&2; exit 2;;
  esac
  shift
done
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) IZ2S_ARCH=x64;; Linux:aarch64|Linux:arm64) IZ2S_ARCH=arm64;;
  *) echo 'Open Install.cmd on Windows or Install.command on macOS. This entry point supports Linux x64/ARM64.' >&2; exit 1;;
esac
iz2s_missing() {
  for iz2s_tool in python3 curl tar xz ssh; do
    command -v "$iz2s_tool" >/dev/null 2>&1 || echo "$iz2s_tool"
  done
  python3 -c "from PIL import Image,ImageDraw,ImageFont,ImageOps;assert hasattr(ImageDraw.ImageDraw,'textbbox') and hasattr(ImageFont.FreeTypeFont,'getlength') and hasattr(ImageOps,'contain')" >/dev/null 2>&1 || echo python-pillow
}
IZ2S_MISSING=$(iz2s_missing)
IZ2S_NODE_OK=0
if command -v node >/dev/null 2>&1 && node -e 'const [a,b]=process.versions.node.split(".").map(Number);process.exit(a>20||(a===20&&b>=20)?0:1)'; then IZ2S_NODE_OK=1; fi
if [ "$IZ2S_CHECK" -eq 1 ]; then
  echo "Platform: linux-$IZ2S_ARCH"
  if [ -n "$IZ2S_MISSING" ]; then printf 'Missing dependencies:\n%s\n' "$IZ2S_MISSING"; else echo 'Runtime dependencies: ready'; fi
  if [ "$IZ2S_NODE_OK" -eq 1 ]; then echo 'Node.js: ready'; else echo 'Node.js: installer will download and verify a private Node.js 24 runtime'; fi
  echo 'Fonts are bundled. ip, ping, findmnt and shader compilers are not runtime prerequisites.'
  [ -z "$IZ2S_MISSING" ] && [ "$IZ2S_NODE_OK" -eq 1 ]; exit $?
fi
if [ -n "$IZ2S_MISSING" ]; then
  if [ "$IZ2S_SYSTEM" -eq 0 ]; then printf 'Missing dependencies; no system changes requested:\n%s\n' "$IZ2S_MISSING" >&2; exit 1; fi
  if [ "$(id -u)" -eq 0 ]; then IZ2S_SUDO=; else IZ2S_SUDO=sudo; fi
  echo 'Installing runtime dependencies. Your package manager may request a password.'
  if command -v apt-get >/dev/null 2>&1; then
    $IZ2S_SUDO apt-get update
    $IZ2S_SUDO apt-get install -y python3 python3-pil curl ca-certificates tar xz-utils openssh-client
  elif command -v pacman >/dev/null 2>&1; then
    echo 'Arch requires a synchronized system upgrade; review the package-manager prompt.'
    $IZ2S_SUDO pacman -Syu --needed python python-pillow curl ca-certificates tar xz openssh
  elif command -v dnf >/dev/null 2>&1; then
    $IZ2S_SUDO dnf install -y python3 python3-pillow curl ca-certificates tar xz openssh-clients
  else
    printf 'Install these portable dependencies with your package manager, then run again:\n%s\n' "$IZ2S_MISSING" >&2
    exit 1
  fi
  IZ2S_MISSING=$(iz2s_missing)
  [ -z "$IZ2S_MISSING" ] || { printf 'Dependencies are still missing:\n%s\n' "$IZ2S_MISSING" >&2; exit 1; }
fi
IZ2S_VERSION=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["version"])' "$IZ2S_SOURCE/package.json")
IZ2S_DEST=${IZ2S_DEST:-${XDG_DATA_HOME:-$HOME/.local/share}/iz2s-platform/releases/$IZ2S_VERSION-$(date -u +%Y%m%d%H%M%S)}
case "$IZ2S_DEST" in /*) ;; *) echo '--prefix must be an absolute path' >&2; exit 1;; esac
if [ -e "$IZ2S_DEST" ] || [ -L "$IZ2S_DEST" ]; then echo 'Destination exists. Choose a new folder; existing settings are never overwritten.' >&2; exit 1; fi
mkdir -p "$(dirname -- "$IZ2S_DEST")"
IZ2S_TMP=$(mktemp -d "$(dirname -- "$IZ2S_DEST")/.iz2s-install-XXXXXX")
trap 'rm -rf -- "$IZ2S_TMP"' EXIT HUP INT TERM
python3 - "$IZ2S_SOURCE" "$IZ2S_TMP" <<'PY'
import importlib.util,sys
from pathlib import Path
source,destination=map(Path,sys.argv[1:])
spec=importlib.util.spec_from_file_location('iz2s_release',source/'distribution/release.py')
exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
exporter.copy_sources(destination)
PY
if [ "$IZ2S_NODE_OK" -eq 0 ]; then
  IZ2S_BASE=https://nodejs.org/dist/latest-v24.x
  curl --fail --location --proto '=https' --tlsv1.2 --connect-timeout 15 "$IZ2S_BASE/SHASUMS256.txt" -o "$IZ2S_TMP/node-shasums.txt"
  IZ2S_ARCHIVE=$(python3 - "$IZ2S_TMP/node-shasums.txt" "$IZ2S_ARCH" <<'PY'
import re,sys
rows=[line.split()[1] for line in open(sys.argv[1]) if re.fullmatch(r'node-v24\.\d+\.\d+-linux-'+sys.argv[2]+r'\.tar\.xz',line.split()[-1])]
if len(rows)!=1:raise SystemExit('Cannot resolve the official Node.js archive')
print(rows[0])
PY
)
  curl --fail --location --proto '=https' --tlsv1.2 --connect-timeout 15 "$IZ2S_BASE/$IZ2S_ARCHIVE" -o "$IZ2S_TMP/$IZ2S_ARCHIVE"
  python3 - "$IZ2S_TMP" "$IZ2S_ARCHIVE" <<'PY'
import hashlib,sys
from pathlib import Path
folder=Path(sys.argv[1]);name=sys.argv[2]
expected=next(line.split()[0] for line in (folder/'node-shasums.txt').read_text().splitlines() if line.split()[-1]==name)
if hashlib.sha256((folder/name).read_bytes()).hexdigest()!=expected:raise SystemExit('Node.js checksum verification failed')
PY
  mkdir -p "$IZ2S_TMP/data/runtime/node"
  tar -xJf "$IZ2S_TMP/$IZ2S_ARCHIVE" --strip-components=1 -C "$IZ2S_TMP/data/runtime/node"
  rm -- "$IZ2S_TMP/$IZ2S_ARCHIVE" "$IZ2S_TMP/node-shasums.txt"
fi
IZ2S_ROOT="$IZ2S_TMP" IZ15_ROOT="$IZ2S_TMP" "$IZ2S_TMP/tools/node" -e 'require(process.argv[1]).publish(process.argv[2]).then(r=>{if(r.errors.length)throw Error(JSON.stringify(r.errors))})' "$IZ2S_TMP/sdk/publish-modules.cjs" "$IZ2S_TMP"
IZ2S_ROOT="$IZ2S_TMP" IZ15_ROOT="$IZ2S_TMP" "$IZ2S_TMP/tools/node" "$IZ2S_TMP/distribution/verify.cjs"
IZ2S_ROOT="$IZ2S_TMP" IZ15_ROOT="$IZ2S_TMP" "$IZ2S_TMP/tools/node" "$IZ2S_TMP/hardware/dependencies.cjs" --host-only --ssh
mv -- "$IZ2S_TMP" "$IZ2S_DEST"
trap - EXIT HUP INT TERM
if [ "$IZ2S_DESKTOP" -eq 1 ]; then "$IZ2S_DEST/tools/node" "$IZ2S_DEST/desktop/install.cjs" --menu-only; fi
printf '\nInstalled: %s\nLaunch: %s/start.sh\n' "$IZ2S_DEST" "$IZ2S_DEST"
echo 'The installer did not send input to a console or replace any device firmware.'
if [ "$IZ2S_LAUNCH" -eq 1 ]; then
  mkdir -p "$IZ2S_DEST/data/state"
  "$IZ2S_DEST/tools/node" -e 'const fs=require("node:fs"),p=require("node:path"),s=require("node:child_process"),r=process.argv[1];const log=fs.openSync(p.join(r,"data/state/startup.log"),"a",0o600);const c=s.spawn(process.execPath,[p.join(r,"start.cjs")],{cwd:r,detached:true,stdio:["ignore",log,log]});c.once("error",e=>{console.error(e.message);process.exitCode=1});c.once("spawn",()=>c.unref());fs.closeSync(log)' "$IZ2S_DEST"
fi
