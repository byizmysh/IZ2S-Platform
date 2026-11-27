#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")"
nx=${LIBOUNCE:-../../../../api_reimplement/libounce-nx/nx}
mkdir -p build/tests
cxx=${CXX:-g++}
# libnx's HIPC parser reads the kernel PID at an unaligned offset, as allowed
# on AArch64. Keep other ASan/UBSan checks enabled for the host fixtures.
flags="-std=gnu++20 -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=address,undefined -fno-sanitize=alignment -D__SWITCH__ -D__SWITCH2__ -DLIBNX_HOST_TEST_TLS"
$cxx $flags -I"$nx/include" -I../libkaihen/include source/wire.cpp tests/wire_test.cpp -o build/tests/wire
build/tests/wire
$cxx $flags -I"$nx/include" source/image.cpp tests/image_test.cpp -o build/tests/image
build/tests/image
$cxx $flags -I"$nx/include" -I../libkaihen/include source/wire.cpp source/session.cpp tests/session_test.cpp -o build/tests/session
$cxx $flags -I"$nx/include" -I../libkaihen/include source/mapping.cpp source/image.cpp tests/mapping_test.cpp -o build/tests/mapping
build/tests/mapping
build/tests/session
$cxx $flags -I"$nx/include" -I../libkaihen/include source/mapping.cpp source/image.cpp tests/mapping_test.cpp -o build/tests/mapping
build/tests/mapping
echo 'RO wire, image, session and mapping tests: PASS'
