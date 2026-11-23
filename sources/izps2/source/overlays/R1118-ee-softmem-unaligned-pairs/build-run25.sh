#!/usr/bin/env bash
set -euo pipefail
out=$(cd "$(dirname "$0")" && pwd -P)
p=$(dirname "$out")
ref="$p/R890-ee-reference-load"
python3 "$out/build-frontend.py" > "$out/frontend-build.log" 2>&1
python3 "$out/build-sdk.py" \
  --output "$out" --revision R1118 --reuse-launcher-from "$p/R949-ee-warm-publication-menu-bound" --ee-native-poll-keep-vu-cache --limiter-audit --queue-wait-audit --ee-event-audit --buffered-state-writes --vm-runner-source "$out/candidate-source/ps2_vm_runner.cpp" --native-fatal-record --menu-assets "$p/R949-ee-warm-publication-menu-bound/menu-assets" --jit-provider "$ref/source/jit-cache.c" \
  --batch-pool --pool-pair "$ref/source" --kaihen-jit-source "$p/R912-kaihen-service-ready/source/kaihen-jit.c" \
  --ram-fallthrough --defer-ee --ee-startup-trace --iop-clock --iop-quiet-ee-event --ee-warm --ee-warm-ranges \
  --frame-pixel-samples --bounded-present --present-isolation --frame-windows "$p/R968-iop-entry-selection/candidate-source/video.cpp" --ee-neon-smc --ee-dispatch-known-pc \
  --ee-dispatch-aligned-lut --ee-dispatch-direct-ram --ee-dispatch-aligned-ram --ee-dispatch-ab \
  --ee-indexed-pins --ee-shared-memory-calls --ee-mmio-block-tail --ee-tagged-pages \
  --ee-fold-ram-offset --ee-fpr-memory-pairs --ee-native-poll-trial --ee-native-poll-cache --ee-native-poll-simd --ee-native-poll-pages --ee-native-poll-compare --ee-native-poll-address-chain --ee-native-poll-keep-tfx --ee-pending-load-home \
  --ee-direct-quad-load --ee-retain-quad-source --ee-callret-cursor --ee-callret-post-index \
  --ee-pinned-branch-target --iop-wait-budget --iop-known-pc-dispatch --iop-event-fast-entry \
  --host-buffer-scale 32 --vk-on-demand-rings --vk-stream-pressure --tfx-precompile-cost --tfx-join-shader --disc-cache-hints --disc-cache-4x --disc-cache-index \
  --disc-cache-large --wide-mtgs 16 --session-timing --iop-session-timing --ee-finalize-batch \
  --ee-incoming-batch --ee-publication-union --gpr-pool-scan > "$out/build.log" 2>&1
python3 "$out/stage-local.py" > "$out/stage.log" 2>&1
node "$out/attach-frontend.cjs" > "$out/frontend-package.log" 2>&1
python3 "$out/verify-prelinked.py" > "$out/prelinked-verification.json"
