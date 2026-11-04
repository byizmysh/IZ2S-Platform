# Controller mailbox and frame visibility

Controllers is a built-in shell screen (kind 7, mode 3). It lists Automatic, up to nine detected sources, two PC profiles and Back. D-pad/stick, A/B and touch operate it. Settings are persisted by the host, not the shader.

## Mailbox ABI

The mailbox occupies 256 bytes at `mapped+0x701c00`, magic `0x48435a49`, version 1.

| Offset | Field | Writer |
|---|---|---|
| 0, 4 | magic, version | Host |
| 8 | requestSequence | GPU |
| 12 | ackSequence | Host |
| 16, 20 | requestSource, requestProfile | GPU |
| 24 | status | Host |
| 28, 32 | saved source/profile | Host |
| 36 | connectedMask: bit0 Pro, bit1 Joy-Con/Handheld | Host |
| 40, 44, 48, 52 | cursor, touchHeld, repeat, scroll | GPU |
| 56, 60 | effectiveSource, capabilities | Host |
| 64 | controllerCount, at most 9 | Host |
| 68-139 | nine `{id:u32,style:u32}` entries | Host |
| 144 | heartbeat | GPU |

Source low byte: 0 Automatic, 1 Pro, 2 Joy-Con/Handheld. Bits 8-15 encode ID+1; zero means automatic ID. IDs 0-7 and 32 (Handheld) are supported. SDK style bits: FullKey=1, Handheld=2, JoyDual=4, JoyLeft=8, JoyRight=16. Profiles: 1 Nintendo Pro, 2 Xbox; capabilities uses corresponding bits 1 and 2. Status: 0 ready, 1 applied, 2 unavailable, 3 error, 4 needsPlugin, 5 needsRestart.

Publish payload before sequence using memoryBarrierBuffer(). Increment sequence while skipping zero. Do not publish another request before requestSequence equals ackSequence. Heartbeat increments once per compute frame while Controllers is open; input polling is independent of catalog refresh.

## Black screen until a fresh frame

Stream header: `mapped+0x700c80`. Visibility is **+164** (`SB+41`): 0 hides, 1 shows. **+160 is application kind**, not visibility.

A closed gate renders black and hides stale video, HUD and notifications. An explicitly opened exit menu stays available. Compute, CRC and ACK continue. The host closes the gate on source changes, pause and dock/handheld transitions, even without an IP change. Only a fresh verified frame reopens it.

GPU frame counters also stop in HOME; the shader cannot independently detect a long suspension using a monotonic clock. The host must update visibility.

## Package updates

Return to the launcher before publishing GPU updates. The runtime stops and joins its worker at a batch boundary, installs verified packages, rebuilds the shell/pipelines and command buffers, restores scratch and resumes with the new storage and command handles. Never overwrite handles or build replacement pipelines while the worker is rendering.

Packages sharing shader fragments must be updated consistently; conflicting bytes for the same fragment name are rejected. Host JavaScript changes require a fresh host session rather than merely replacing a console payload. Use the supported module manager instead of an old ad-hoc live replacement script.

Local Vulkan tests verify shader execution and mailbox behavior; they do not create input devices or send commands to Switch. They do not replace physical controller and console transition tests.
