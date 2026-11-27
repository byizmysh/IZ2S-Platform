#!/usr/bin/env python3
"""Prepare a local OunceVK build without modifying the installed SDK."""
from pathlib import Path
import shutil
import shlex

root = Path(__file__).resolve().parent
src = root / 'ouncevk'
local = root / 'local-build'
local.mkdir(exist_ok=True)
sdk = Path('/home/k2/Antigravity/MyLoveSwitch/iz2s-panel/iz2s-platform-run23/engines/izhen/sdk/devkitpro')
tc = root / 'toolchain'
crosslib = local / 'cross-lib'
crosslib.mkdir(exist_ok=True)
# Matches upstream's Dockerfile: this static Horizon target has no libdl.
(crosslib / 'libdl.a').write_bytes(b'!<arch>\n')
include = local / 'cross-include'
include.mkdir(exist_ok=True)
for name in ('xf86drm.h', 'xf86drmMode.h'):
    shutil.copy2(Path('/usr/include') / name, include / name)
shutil.copytree('/usr/include/libdrm', include, dirs_exist_ok=True)
shutil.copytree('/usr/include/libdrm', include / 'libdrm', dirs_exist_ok=True)
shutil.copytree(src / 'switch2/toolchain/cross-include', include, dirs_exist_ok=True)
pcs = local / 'host-pkgconfig'
pcs.mkdir(exist_ok=True)
for name in ('LLVMSPIRVLib.pc', 'libclc.pc'):
    paths = list((tc / 'usr').rglob(name))
    if paths:
        (pcs / name).write_text(paths[0].read_text().replace('prefix=/usr', f'prefix={tc}/usr'))

def executable(name, text):
    path = local / name
    path.write_text(text)
    path.chmod(0o755)
    return path

pkg = executable('cross-pkg-config', '#!/bin/sh\n'
    'export PKG_CONFIG_DIR=\nexport PKG_CONFIG_PATH=\n'
    f'export PKG_CONFIG_LIBDIR={shlex.quote(str(sdk / "portlibs/switch/lib/pkgconfig"))}\n'
    'exec pkg-config "$@"\n')
(local / 'host.ini').write_text((src / 'switch2/crossfiles/host.ini').read_text().replace('/usr/bin/llvm-config-15', '/usr/bin/llvm-config'))
replacements = {
    '/opt/devkitpro': str(sdk),
    '/opt/switch-cross-include': str(include),
    '/ounce-nx': str(sdk / 'libounce'),
    '/work/switch2/toolchain/rust/rustc-wrapper.sh': str(local / 'rustc-wrapper.sh'),
}
for name in ('horizon.cross', 'rust.cross'):
    text = (src / 'switch2/crossfiles' / name).read_text()
    for before, after in replacements.items():
        text = text.replace(before, after)
    text = text.replace(str(sdk / 'portlibs/switch/bin/aarch64-none-elf-pkg-config'), str(pkg))
    if name == 'horizon.cross':
        text = text.replace('link_args = cpu_args + [', f"link_args = cpu_args + [\n    '-L{crosslib}',")
    (local / name).write_text(text)
for name in ('build-sysroot.sh', 'rustc-wrapper.sh'):
    text = (src / 'switch2/toolchain/rust' / name).read_text()
    text = text.replace('/work/switch2/toolchain/rust', str(src / 'switch2/toolchain/rust'))
    text = text.replace('/opt/devkitpro', str(sdk)).replace('cargo "+$TOOLCHAIN"', 'cargo')
    executable(name, text)
for name, relative in [('build-host-tools.sh', 'switch2/toolchain/build-host-tools.sh'),
                       ('build-native.sh', 'switch2/build/configure-native-headless.sh')]:
    text = (src / relative).read_text()
    for cross in ('host.ini', 'horizon.cross', 'rust.cross'):
        text = text.replace(f'$SRC/switch2/crossfiles/{cross}', str(local / cross))
    text = text.replace('-j 6', '-j 3')
    if name == 'build-native.sh':
        text = text.replace('    --buildtype release', '    --buildtype release\n    -Ddefault_library=static')
    text = text.replace('ninja -C "$HOST_BUILD"', 'ninja -j 3 -C "$HOST_BUILD"')
    # Refresh our private headers, never /opt or the system SDK.
    text = text.replace('/opt/switch-cross-include/', str(include) + '/')
    executable(name, text)
env = {
    'SRC': str(src), 'NVK_LIBOUNCE_NX': str(sdk / 'libounce'),
    'PKG_CONFIG_PATH': str(pcs),
    'LD_LIBRARY_PATH': str(tc / 'usr/lib'),
    'CARGO_HOME': str(tc / 'cargo-home'),
    'LIBCLANG_PATH': '/usr/lib',
}
text = '#!/bin/bash\nset -euo pipefail\n'
for key, value in env.items():
    text += f'export {key}={shlex.quote(value)}\n'
text += f'export PATH={shlex.quote(str(tc / "rust/bin") + ":" + str(root / "toolchain-venv/bin") + ":" + str(sdk / "devkitA64/bin"))}:"$PATH"\n'
text += 'exec "$@"\n'
executable('env.sh', text)
print(local)
