"""Exercise the released IZDOOM mixer on desktop Vulkan; no console access.

Pass the extracted corresponding IZDOOM source bundle (including build assets).
The source bundle's GPU harness must already be built. No software is installed.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct


def run(source):
    source = source.resolve()
    spec = importlib.util.spec_from_file_location("izdoom_gpu_check", source / "tests/check-gpu.py")
    gpu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gpu)
    shader = source / "build/module/switch/after.comp.spv"
    if not (source / "build/gpu-stage").is_file():
        raise RuntimeError("Build the source bundle's desktop GPU harness first")

    memory, state = gpu.initial()
    memory, state = gpu.gpu(memory, state)  # clear
    gpu.cw(state, 26, 1)
    memory, state = gpu.gpu(memory, state)  # prime
    assert gpu.gw(state, 9) == 1 and gpu.gw(state, 23) == 8192
    gpu.cw(state, 14, 1)
    anchor = 1520833
    for tick in (1000000, anchor):
        struct.pack_into("<III", state, 0x700D70, tick, 0, 31250000)
        gpu.cw(state, 26, 1)
        memory, state = gpu.gpu(memory, state)
    assert gpu.gw(state, 9) == 2 and gpu.gw(state, 11) == 2
    gpu.cw(state, 15, anchor)
    gpu.cw(state, 16, 0)
    gpu.cw(state, 8, 1)

    rows = []
    # A display acquire waits 200 ms while the already-started DSP keeps playing.
    dsp_frames = 9600
    for name, tick in (("timestamp-before-display-wait", anchor),
                       ("timestamp-after-display-wait", anchor + 6250000)):
        candidate = bytearray(state)
        struct.pack_into("<III", candidate, 0x700D70, tick, 0, 31250000)
        gpu.cw(candidate, 26, 1)
        _, result = gpu.gpu(memory, candidate)
        assert gpu.gw(result, 12) == 0
        written = gpu.gw(result, 23)
        rows.append({"case": name, "simulatedDspFrames": dsp_frames,
                     "mixerElapsedFrames": gpu.gw(result, 21),
                     "writtenFrames": written, "futureLeadFrames": written - dsp_frames,
                     "recoveries": gpu.gw(result, 28)})
    assert rows[0]["futureLeadFrames"] < 0
    assert rows[0]["recoveries"] == 0  # stale time conceals the missed deadline
    assert rows[1]["futureLeadFrames"] == 5600
    assert rows[1]["recoveries"] == 1
    return {"passed": True, "actualGpuShader": True, "consoleAccessed": False,
            "dspIsSimulated": True, "displayWaitMilliseconds": 200,
            "shaderSHA256": hashlib.sha256(shader.read_bytes()).hexdigest(), "cases": rows,
            "limit": "Cannot recover sound already missed during the wait; acoustic drift and DSP prefetch still require a console test."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = json.dumps(run(args.source), indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report)
    print(report, end="")
