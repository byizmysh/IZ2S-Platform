#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
source sdk/tools/toolchain.sh
"$NODE" -e "require('./sdk/build.cjs').headers('shells/izpslauncher/server/shaders').catch(e=>{console.error(e);process.exitCode=1})"
python3 shells/izpslauncher/server/launcher-text.py
for stage in vert frag comp; do
 "$GLSLANG" -V -Os -g0 --target-env vulkan1.0 "shells/izpslauncher/server/shaders/launcher.$stage" -o "shells/izpslauncher/server/shaders/launcher.$stage.spv"
 "$SPIRV_VAL" --target-env vulkan1.0 "shells/izpslauncher/server/shaders/launcher.$stage.spv"
done
