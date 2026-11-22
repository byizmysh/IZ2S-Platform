#!/bin/sh
set -eu
: "${IZ_LINUX_SYSROOT:?Set the AArch64 Linux sysroot}"
: "${IZ_SB4_INCLUDE:?Set the libsb4 include directory}"
: "${IZ_QEMU_AARCH64:?Set qemu-aarch64 executable}"
module=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
release=${1:?Pass the Store release directory}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang --target=aarch64-linux-gnu --sysroot="$IZ_LINUX_SYSROOT" -fuse-ld=lld \
    -std=gnu11 -O2 -Wall -Wextra -Werror -fno-pie -no-pie -nostdlib -I "$IZ_SB4_INCLUDE" \
    "$IZ_LINUX_SYSROOT/lib/crt1.o" "$IZ_LINUX_SYSROOT/lib/crti.o" "$module/tests/iza-lifecycle.c" \
    -Wl,--no-as-needed "$IZ_LINUX_SYSROOT/lib/libc.so.6" "$IZ_LINUX_SYSROOT/lib/libgcc_s.so.1" \
    "$IZ_LINUX_SYSROOT/lib/libc_nonshared.a" "$IZ_LINUX_SYSROOT/lib/ld-linux-aarch64.so.1" \
    "$IZ_LINUX_SYSROOT/lib/crtn.o" -Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1 -o "$work/test"
python3 - "$release/iz2s-cheese-iza-sd.zip" "$work" <<'PY'
from pathlib import Path
import sys
from zipfile import ZipFile
with ZipFile(sys.argv[1]) as z:
    names=[n for n in z.namelist() if n.endswith('.izdata')]
    assert len(names)==1
    Path(sys.argv[2], 'engine.izdata').write_bytes(z.read(names[0]))
PY
"$IZ_QEMU_AARCH64" -L "$IZ_LINUX_SYSROOT" "$work/test" "$module/switch/iza/native.bin" \
    "$work/engine.izdata" "$module/switch/iza/assets/data.bin"
