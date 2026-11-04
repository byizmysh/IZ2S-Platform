#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
source sdk/tools/toolchain.sh
"$NODE" -e "require('./sdk/build.cjs').headers('shells/izlauncher/server/shaders').catch(e=>{console.error(e);process.exitCode=1})"
python3 shells/izlauncher/server/launcher-text.py
for stage in vert frag comp; do
 "$GLSLANG" -V -Os -g0 --target-env vulkan1.0 "shells/izlauncher/server/shaders/launcher.$stage" -o "shells/izlauncher/server/shaders/launcher.$stage.spv"
 "$SPIRV_VAL" --target-env vulkan1.0 "shells/izlauncher/server/shaders/launcher.$stage.spv"
done
