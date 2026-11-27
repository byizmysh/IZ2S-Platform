#!/usr/bin/env python3
"""Package Vulkan only, using upstream's prelink/export rules."""
from pathlib import Path
import subprocess
import json
import hashlib

root = Path(__file__).resolve().parent
src = root / 'ouncevk'
sdk = Path('/home/k2/Antigravity/MyLoveSwitch/iz2s-panel/iz2s-platform-run23/engines/izhen/sdk/devkitpro')
prefix = sdk / 'devkitA64/bin/aarch64-none-elf'
build = src / 'build/native-headless'
out = root / 'driver'
out.mkdir(exist_ok=True)
upstream = (src / 'switch2/build/package-portlib.sh').read_text()
archives = upstream.split('DRIVER_ARCHIVES="', 1)[1].split('"', 1)[0].split()
shims = upstream.split('POSIX_SHIMS="', 1)[1].split('"', 1)[0].split()
members = subprocess.check_output([str(prefix) + '-ar', 't', str(build / 'src/nouveau/vulkan/libnvk.a')], cwd=build, text=True).splitlines()
members = sorted(set(str((build / name).resolve()) for name in members))
zlib = build / 'subprojects/zlib-1.3.1/libz.a'
assert zlib.is_file(), zlib
obj = out / 'vulkan.o'
commands = [[str(prefix) + '-ld', '-r', '--force-group-allocation', '-o', str(obj),
             *members, '--start-group', *[str(build / a) for a in archives], str(zlib), '--end-group']]
args = [str(prefix) + '-objcopy', '--wildcard']
for symbol in ['vk[A-Z]*', 'ouncevk_profile_*', '__nx_gpu_exit']:
    args += ['--keep-global-symbol=' + symbol]
for symbol in shims:
    args += ['--keep-global-symbol=' + symbol, '--weaken-symbol=' + symbol]
commands.append([*args, str(obj)])
commands.append([str(prefix) + '-ar', 'rcs', str(out / 'libouncevk.a'), str(obj)])
for command in commands:
    subprocess.run(command, check=True)
archive = out / 'libouncevk.a'
(out / 'package-record.json').write_text(json.dumps({
    'commands': commands,
    'archive': {'bytes': archive.stat().st_size, 'sha256': hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()},
    'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=src, text=True).strip(),
    'baselineUpstream': 'f5afcd27a98d47a35b7091b097ffd7f46b0be3e4',
    'patches': [p.stem for p in sorted((root / 'patches').glob('*.patch'))],
    'variant': 'static-vulkan-only',
}, indent=2) + '\n')
print(archive)
