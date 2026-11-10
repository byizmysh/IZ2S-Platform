# Native engine presentation (ABI 6)

ABI 6 preserves the ABI 1–5 prefixes and adds `graphics`,
`presentation_acquire` and `presentation_release`. An ordinary module keeps
its existing ABI and rendering behavior. The same supervisor serves both shells.

`graphics` describes the current renderer's Vulkan base, instance, physical
device, device, queue, swapchain, command pool, live images and views. Its
function table uses the public `IZ_GFX_*` order and the supervisor's already
verified function bindings. The memory-property bytes are a bootstrap snapshot,
not proof of available memory or permission to use a newly mapped allocation.
These resources are borrowed and immutable. Never destroy, replace or free them.
Additional engine-specific entry points still need independent byte/mapping
qualification; the Vulkan base alone does not qualify an executable span.

The ABI 6 manifest selects all native, image, cooperative, worker and presentation
flags. Incomplete flag combinations are rejected. Before loading code, the
supervisor rejects missing graphics identities, bindings, images/views and
invalid memory-type/heap counts without allocating an application.

Load the engine through `image_begin`/`image_step` first. From a serialized INIT
or FRAME callback, call `presentation_acquire(app, &ticket)` while no worker is
owned and the image is ready. The previous supervisor presentation has completed
its actual `QueueWaitIdle`; the next supervisor frame will not acquire, submit
or present while this lease is live. Tickets have a distinct domain and change
on reacquisition and repeat launch. Duplicate acquire, cross-domain and stale
release are rejected.

Then start the owned engine worker. ABI 6 rejects `worker_begin` before the lease
is acquired. The supervisor continues its CPU input sweep and serialized FRAME
callbacks, using a 16,666,667 ns sleep while engine presentation is owned.
Two R3 edges still request STOP. It never calls FRAME after STOP starts.

STOP must request the engine's real exit, validate engine-specific nested
worker/device/DSP/JIT cleanup receipts, and poll and join the owned entry worker.
Only then release presentation. `presentation_release` requires the exact live
ticket and no owned worker; it calls the actual `QueueWaitIdle`. A nonzero queue
result retains the application and all owners. Successful release revokes the
ticket and resumes ordinary supervisor presentation. The image may then be
released cooperatively. STOP success with a live presentation lease retains all
owners instead of drawing a menu or freeing resources that might still be used.

Bootstrap copies these identities and the property snapshot from its existing
renderer setup. It adds no SDK/Vulkan verification requests. The packed context
grows from 7,848 to 8,392 bytes, still within one 32,768-byte native write chunk;
its original stack offsets and every ABI 1–5 prefix remain unchanged.

Local evidence: `node tools/sd-launcher/presentation-local.cjs`. Two real host RX
programs and a real pthread run through the production supervisor, image loader
and worker API, with SDK/FS/Vulkan boundaries modeled. Checks cover continued
HID, exclusive engine presentation, double R3, clean return and repeat, early
cancel, known worker-create rejection, a leaked lease at STOP, failed queue idle,
and sixteen metadata rejections before allocation. AArch64 programs are compiled
and PIC checked, not executed. This is preparation for a native PS2 controller;
it does not make the existing host PS2 wrapper autonomous or qualify hardware.
