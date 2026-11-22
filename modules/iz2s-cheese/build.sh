#!/bin/sh
set -eu

module_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
platform_dir=${IZ2S_PLATFORM_ROOT:-$(CDPATH='' cd -- "$module_dir/../.." && pwd)}
font_dir=${IZ_STORE_FONT_DIR:-"$platform_dir/language/fonts"}
DEVKITPRO=${IZ_DEVKITPRO:-${DEVKITPRO:?Set DEVKITPRO or IZ_DEVKITPRO to the installed Switch 2 SDK}}
export DEVKITPRO
cross=${IZ_AARCH64_PREFIX:-"$DEVKITPRO/devkitA64/bin/aarch64-none-elf-"}
include="$DEVKITPRO/../sb4/include"
packer=${IZ_RELOCATABLE_PACKER:-"$platform_dir/../iz2s-platform-run23/tools/sd-launcher/relocatable.py"}
if [ -f "$platform_dir/core/archive.cjs" ]; then
    output=${IZ_STORE_OUT:-"$platform_dir/dist/iz2s-cheese"}
else
    output=${IZ_STORE_OUT:-"$module_dir/.build"}
fi
case "${1:-both}" in
    both) build_nro=1; build_iza=1 ;;
    nro) build_nro=1; build_iza=0 ;;
    iza) build_nro=0; build_iza=1 ;;
    *) echo "Usage: sh build.sh [both|nro|iza]" >&2; exit 2 ;;
esac
if [ "$build_iza" = 1 ] && [ ! -f "$platform_dir/core/archive.cjs" ]; then
    echo "Set IZ2S_PLATFORM_ROOT to a checkout of IZ2S Platform to build IZA." >&2
    exit 2
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/sd" "$work/romfs" "$output"
python3 -B "$module_dir/assets.py" "$font_dir" "$work/romfs" "$work/icon.jpg"
cp "$module_dir/licenses/DejaVu.txt" "$work/romfs/font-license.txt"
if [ "$build_nro" = 1 ]; then
    cp "$DEVKITPRO"/libounce/licenses/*.txt "$work/romfs/"
    cp "$module_dir/licenses/Octicons.txt" "$work/romfs/"
    "${cross}gcc" -std=gnu11 -O2 -Wall -Wextra -Werror -mcpu=cortex-a78c+crypto -mtp=soft \
        -fPIE -ffunction-sections -fdata-sections -D__SWITCH__ -D__SWITCH2__ -DIZ_STORE_STANDALONE -DIZ_STORE_VERSION=\"0.8.1\" \
        -isystem "$DEVKITPRO/libounce/include" -specs="$DEVKITPRO/libounce/switch.specs" \
        -L"$DEVKITPRO/libounce/lib" "$module_dir/switch/main.c" "$module_dir/switch/store.c" \
        "$module_dir/switch/ui.c" "$module_dir/switch/download.c" "$module_dir/switch/installed-artwork.c" \
        -lounce -o "$work/standalone.elf"
    version=$(node -p 'require(process.argv[1]).version' "$module_dir/module.json")
    tools="$DEVKITPRO/tools/bin"
    "$tools/nacptool" --create 'IZ2S Cheese' IZMYSH "$version" "$work/standalone.nacp"
    "$tools/elf2nro" "$work/standalone.elf" "$output/iz2s-cheese.nro" \
        --nacp="$work/standalone.nacp" --icon="$work/icon.jpg" --romfsdir="$work/romfs"
    python3 - "$output/iz2s-cheese.nro" "$output/iz2s-cheese-nro-sd.zip" <<'PY'
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
import os
import sys

source, target = map(Path, sys.argv[1:])
stage = target.with_suffix('.tmp')
with ZipFile(stage, 'w', ZIP_DEFLATED) as pack:
    pack.write(source, 'switch/iz2s-cheese/iz2s-cheese.nro')
os.replace(stage, target)
print(target)
PY
fi

if [ "$build_iza" = 1 ]; then
python3 -B "$module_dir/switch/iza/build-engine.py" --devkitpro "$DEVKITPRO" --include "$include" \
    --cross "$cross" --packer "$packer" --work "$work/engine"
mkdir -p "$module_dir/switch/iza/assets"
cp "$module_dir/brand/cheese-icon.png" "$module_dir/switch/iza/assets/icon.png"
python3 -B "$module_dir/switch/iza/assets.py" "$work/romfs" "$module_dir/switch/iza/assets/data.bin"
python3 - "$module_dir/switch/iza/assets/state.bin" <<'STATE'
from pathlib import Path
import sys
Path(sys.argv[1]).write_bytes(bytes(1024))
STATE
for stage in vert frag comp; do
    glslangValidator -V -Os -g0 --target-env vulkan1.0 "$module_dir/switch/iza/app.$stage" -o "$module_dir/switch/iza/app.$stage.spv"
    spirv-val --target-env vulkan1.0 "$module_dir/switch/iza/app.$stage.spv"
done
"${cross}gcc" -std=c11 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -fPIC -fvisibility=hidden -fno-stack-protector -fno-jump-tables -mbranch-protection=bti \
    -I "$include" -I "$work/engine" -c "$module_dir/switch/iza/entry.c" -o "$work/entry.o"
"${cross}ld" --no-undefined --fatal-warnings -T "$module_dir/switch/iza/entry.ld" "$work/entry.o" -o "$work/entry.elf"
"${cross}objcopy" -O binary -j .text "$work/entry.elf" "$module_dir/switch/iza/native.bin"
node - "$platform_dir" "$module_dir" "$work" <<'JS'
const fs = require('node:fs/promises');
const path = require('node:path');
const [root, moduleDir, work] = process.argv.slice(2);
const archive = require(path.join(root, 'core/archive.cjs'));
const pkg = require(path.join(root, 'core/console/sd-launcher/package.cjs'));
(async () => {
  const packaged = path.join(moduleDir, 'iz2s-cheese.module.zip');
  const prepared = path.join(work, 'prepared');
  const result = await pkg.build(path.join(moduleDir, 'switch/iza'), prepared, {sb4:true, includeLibrary:false});
  if (pkg.decode(result.bytes).nativeAbi !== 7 || result.id !== 'iz2s-cheese') throw Error('Unexpected IZA layout');
  await fs.writeFile(packaged, await archive.pack(moduleDir));
  const folder = path.join(work, 'sd', 'iz2s', 'modules', result.id);
  await fs.mkdir(folder, {recursive:true});
  for (const name of ['launcher.iza', 'sd-bundle.json', ...result.assets.map(asset=>asset.path)])
    await fs.copyFile(path.join(prepared, name), path.join(folder, name));
  console.log(packaged);
})().catch(error => { console.error(error.stack); process.exitCode = 1; });
JS

python3 - "$work/sd" "$output/iz2s-cheese-iza-sd.zip" <<'PY'
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
import os
import sys
root, target = map(Path, sys.argv[1:])
stage = target.with_suffix('.tmp')
with ZipFile(stage, 'w', ZIP_DEFLATED) as pack:
    for source in sorted(root.rglob('*')):
        if source.is_file():
            pack.write(source, source.relative_to(root).as_posix())
os.replace(stage, target)
print(target)
PY

cp "$work/sd/iz2s/modules/iz2s-cheese/launcher.iza" "$output/iz2s-cheese.iza"
cp "$work/sd/iz2s/modules/iz2s-cheese/"*.izdata "$output/"
cp "$work/engine/engine-report.json" "$output/engine-report.json"
fi

if [ "${IZ_STORE_CHECK:-0}" = 1 ]; then
    python3 -B "$module_dir/check.py" "$output" "$build_iza" "$build_nro"
fi
