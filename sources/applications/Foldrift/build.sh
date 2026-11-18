#!/bin/sh
set -eu

module_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
platform_dir=$(CDPATH='' cd -- "$module_dir/../.." && pwd)
switch_dir="$module_dir/switch"
: "${IZ_SB4_INCLUDE:?Set IZ_SB4_INCLUDE to the libsb4 SDK include directory}"
cross=${IZ_AARCH64_PREFIX:-aarch64-none-elf-}
build_tmp=$(mktemp -d)
trap 'rm -rf "$build_tmp"' EXIT

python3 -B "$switch_dir/assets.py"
for stage in vert frag comp; do
    glslangValidator -V -Os -g0 --target-env vulkan1.0 \
        "$switch_dir/shaders/app.$stage" -o "$switch_dir/app.$stage.spv"
    spirv-val --target-env vulkan1.0 "$switch_dir/app.$stage.spv"
done
"${cross}gcc" -std=gnu11 -Os -Wall -Wextra -Werror \
    -ffreestanding -fno-builtin -fPIC -fno-stack-protector -fno-jump-tables \
    -I "$IZ_SB4_INCLUDE" -I "$platform_dir/sdk" -c "$switch_dir/native.c" -o "$build_tmp/deck.o"
"${cross}ld" --no-undefined --fatal-warnings -T "$switch_dir/native.ld" \
    "$build_tmp/deck.o" -o "$build_tmp/deck.elf"
"${cross}objcopy" -O binary -j .text "$build_tmp/deck.elf" "$switch_dir/native.bin"

node - "$platform_dir" "$module_dir" <<'JS'
const fs=require('node:fs/promises'),path=require('node:path');
const [root,moduleDir]=process.argv.slice(2);
const archive=require(path.join(root,'core/archive.cjs'));
const tools=require(path.join(root,'engines/iz2s-core/tools/prepare-sd-app.cjs'));
(async()=>{
    const source=await archive.pack(moduleDir);
    const output=path.join(root,'dist','foldrift');
    const app=await tools.packageModule(archive.inspect(source),output);
    await fs.writeFile(path.join(moduleDir,'iz-file-deck.module.zip'),source);
    await fs.writeFile(path.join(output,app.name),app.bytes);
    console.log(path.join(output,app.name));
})().catch(e=>{console.error(e);process.exitCode=1;});
JS
