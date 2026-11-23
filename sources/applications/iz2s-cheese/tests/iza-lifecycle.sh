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
python3 - "$release/iz2s-cheese-install.iza" "$work" <<'PY'
from pathlib import Path
from struct import unpack_from
import sys
from zipfile import ZipFile

work = Path(sys.argv[2])
with ZipFile(sys.argv[1]) as z:
    engine, = (name for name in z.namelist() if name.endswith('.izdata'))
    (work / 'engine.izdata').write_bytes(z.read(engine))
    launcher = z.read('launcher.iza')
    assert launcher.startswith(b'IZSDAPP1') and unpack_from('<I', launcher, 8)[0] == 2
    for section, name in [(1, 'data.bin'), (10, 'native.bin')]:
        offset, length = unpack_from('<2I', launcher, 176 + 16 * section)
        assert offset >= 512 and length > 0 and offset + length <= len(launcher)
        (work / name).write_bytes(launcher[offset:offset + length])
PY
"$IZ_QEMU_AARCH64" -L "$IZ_LINUX_SYSROOT" "$work/test" "$work/native.bin" \
    "$work/engine.izdata" "$work/data.bin"
