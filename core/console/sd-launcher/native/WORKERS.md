# Native engine workers (ABI 5)

ABI 5 preserves all ABI 1–4 prefixes and adds an optional single engine worker.
Ordinary ABI 1–4 modules retain their previous version and callback table.
The launcher and its dispatcher continue owning input and presentation.
An ABI 5 module must use the cooperative image loader before starting a worker.

`worker_begin(app, imageToken, context, contextBytes, &ticket)` starts only the
entry of the currently admitted, fully loaded engine image. It accepts context
memory inside the parent's native data or the loaded engine's owned data.
Another worker, an unfinished image, an invalid range or a stale image token
is rejected. It does not accept an arbitrary function address.

The worker calls `image.entry(context, 0)` on an SDK pthread. Entry arguments,
an engine-specific stack switch, configuration and resource descriptors are
the parent module's responsibility. The engine return value is retained by
the supervisor's trampoline, which then returns zero to the SDK.

`worker_poll` performs one nonblocking SDK wait. Its result distinguishes a
running worker from a finished one. A finished worker still owns its image and
context. `worker_join` is admitted only after the SDK confirms completion and
the trampoline has published its result. Successful SDK join revokes the
worker ticket and returns the engine result. Stale tickets are rejected.

The module cooperatively requests engine exit through the engine's real
control protocol. In STOP it can return `IZ_NATIVE_STOP_PENDING` while polling
and joining, then release the image through the existing bounded image API.
The dispatcher keeps input and presentation active and never calls FRAME
after STOP begins. An image cannot be released while its worker is owned.
Returning success from STOP with an unjoined worker retains the application.

Successful join proves completion of this entry thread. It does not replace
engine-specific receipts for nested workers, DSP buffers, GPU submissions,
JIT mappings and other resources. The parent must validate those receipts
before unloading executable memory or reporting a clean STOP.

`environment` exposes the session's already verified SDK base, timer frequency
and borrowed FS/RO/process identities. These capabilities remain owned by the
supervisor; modules must not close or free them. Their lifetime is protected
by the worker/image reservation. Bootstrap stores three thread function
addresses it already verifies; no additional verification RPCs were added.

Known create rejection with an absent output remains recoverable. Contradictory
create output, unknown wait, completion without a trampoline result and failed
join poison the owner and retain all code, data and borrowed capabilities.
The supervisor never retries a potentially executed start.

Local evidence: `node tools/sd-launcher/worker-local.cjs`. This executes two
host RX programs and a real pthread through the production supervisor/loader.
Tests cover repeat launch, double R3, ongoing input/presentation, early cancel,
known create rejection and uncertain create/wait/join outcomes. Console device
and kernel boundaries are modeled; AArch64 is compiled and PIC checked.
This is a prerequisite for moving the PS2 startup controller onto Switch;
it does not make the existing host-based PS2 package autonomous by itself.

ABI 6 can transfer presentation to that owned engine worker while retaining
the supervisor's input and STOP loop; see [PRESENTATION.md](PRESENTATION.md).

Parent metadata and the borrowed FS state persist in the retained context
after supervisor errors; see [OWNER-LIFETIME.md](OWNER-LIFETIME.md).
