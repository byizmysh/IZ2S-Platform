# Native PS2 startup preparation

This is the native preparation component for the exact frozen R176 engine.
The installed PS2 package may advance independently; these constants do not
admit another image. The ready-owner launch controller now publishes original
ABI pages and connects them to worker execution and retirement. It does not
publish a standalone SD tile or enter the original PS2 engine yet. Resource
preparation and the game picker still need the SD application's INIT/FRAME/STOP
entry. The sections below describe component milestones; the final section
states the current integration boundary.

`startup.c` constructs eight pages matching the original package's pure ABI
packers: HBX3 context, VM settings, cached JIT domains, preallocated Main,
initial Main receipt, PAD binding, control and log. Logical engine lengths
remain distinct from the page-rounded image ownership lengths. Stack guards
are excluded from the usable 1 MiB stack. Input validation finishes before
the first output write. Output/input aliases, resource overlaps, wrong image
geometry, incomplete nonces and malformed paths are rejected.

`sdk-memory.c` locally checks the current process identity, executable SDK
mapping and the original R176 memalign/free bytecuts before binding callbacks.
It requires no PC request. The original bytecuts and ABI helper sources are
pinned in `profile.json`; they are not inferred from an old session address.

`memory.c` reserves Main first. Once Main is ready, the owner can use ABI 4/5
to load the exact engine image from SD. `after_image` admits that receipt and
then prepares the JIT source, entry stack, dispatch page and optional HWGS
ledger. Each work unit performs at most one allocation, query or free plus
one fresh self-process identity check. Every owned block requires ordinary
RW memory without IPC/device references or cached GPU attributes.

Pre-entry cancellation verifies and frees owned blocks in reverse order,
including cancellation between allocation and its initial mapping check.
A known ENOMEM with zero output unwinds earlier blocks. Contradictory output,
unexpected allocator status, bad mapping, changed process identity or unknown
free retains ownership and admits no retry. Cancellation after entry intent
cannot free anything: actual PS2 Main/VM/AV/storage/JIT cleanup receipts must
be validated by the eventual controller. An outer pthread join alone is
insufficient. The component does not allocate a substitute SDK JIT shadow;
the required coherent cached Vulkan shadow must come from its actual owner.

Run `node tools/sd-launcher/ps2-startup-local.cjs` for the combined local check.
It compares every output byte against the exact unchanged R176 JS packers in
16 address/filesystem/HWGS/GPU configurations, executes the C packer from
actual host RX memory, rejects 1344 invalid calls without output writes and
exercises 16 allocation/cancellation/failure cases with real host allocations.
SDK/kernel boundaries are modeled. The complete preparation library is
compiled for ARM64 and checked for position independence; it is not executed
on Switch by these tests. Performance improvement has not been measured.

`storage.c` now supplies the native inventory and FS/save preparation. It
scans up to three explicit module data roots, keeps distinct same-named games
from different roots, sorts ISO/ELF titles naturally and chooses a 4 MiB BIOS
from the selected root when available. The current host R176 mapping uses
`SD:/IZ2S/Homebrew/Modules/ps2-cold-r113/`; the native component can also scan
the current package's data root. These roots are controller inputs, not a
new requirement to move or recopy the user's games.

Directory enumeration reads 32 entries including file sizes per request,
without a size request for every listed game. Open and GetSize are separate
work units. Selected game/BIOS handles remain owned by the controller and are
borrowed through the original IFile descriptors. Save preparation preserves
the engine's existing `SD:/izmysh/ps2/saves/cards/` location and never closes
the supervisor's borrowed filesystem. A raced directory creation receives
one type recheck rather than an unbounded create/retry loop.

`sdk-storage.c` independently admits the existing SDK send/close functions
and writable controller/scratch mappings. It binds a private child-handle
ledger over the already-open SD filesystem, without a PC preparation call.
Known pre-entry failures close only this component's child handles; unknown
transport, stray handles and an uncertain close retain ownership. After
entry intent, the eventual controller must validate the actual PS2 cleanup
receipts before releasing game/BIOS handles or their buffers.

`node tools/sd-launcher/ps2-storage-local.cjs` runs the production storage
component, exact R176 IFile/save C driver sources and real host files through
a modeled Switch IPC boundary. It checks 17 scenarios, three byte-exact
descriptor pairs against the original JS helper, actual reads beyond 4 GiB
and a persistent 528-byte save roundtrip. The test image is sparse; it is
not a bootable PS2 game. ARM64 is compiled and PIC checked, not executed.
The helper link removes unused filesystem functions while retaining every
public storage entry. Each run preserves its own fixtures and outputs.

Remaining integration: connect this inventory/preparation to the native
application controller and game-picker display; connect the native cached
shadow and presentation handover; add AV/video-surface ownership, context
publication, worker start/exit and
all original engine-specific post-join cleanup receipts. No compatibility
flag should advertise standalone PS2 execution before that path is connected
and verified.

The common supervisor now provides the borrowed current graphics identities
and exclusive presentation handover through [ABI 6](../native/PRESENTATION.md).
Its input and STOP loop continue while an engine owns presentation. This is
available in both shells without extra bootstrap verification requests; it
does not create the PS2 cached JIT shadow or its two video surfaces. The native
PS2 controller must still qualify the exact Vulkan entry points, own those
allocations and validate the original engine's cleanup receipts before using
the common release operation.

`jit-shadow.c` now owns preparation of the original 105 MiB Vulkan cached,
coherent CPU compilation shadow. It rechecks the ordinary SDK source with
attributes zero, freshly enumerates the physical device, queries its memory
properties and requires the qualified type 2 / flags 15 / heap 0 selection.
Only the shadow admits the original opaque attributes `0x60`; neither Main,
SDK JIT source, RX source, stack nor other SDK ownership inherits that policy.
Each work unit performs at most one provider operation and one current-process
identity check. The component never substitutes SDK memory for the GPU shadow
and never promises zero initialization of the mapped GPU allocation.

Known allocation/map rejections with absent outputs unwind owned pre-entry
resources. Contradictory outputs, invalid/overlapping mappings, changed identity
and unknown unmap/free retain ownership and admit no retry. Cancellation after
entry intent cannot free the shadow. The eventual controller must grant
post-entry release only from the actual PS2 VM/JIT nested joins and code-map
receipts; the preparation API deliberately has no shortcut for this.

`sdk-jit-shadow.c` binds the exact seven Vulkan entry points against local RX
queries and pinned original bytecuts, including agreement with the current
renderer's allocate/free addresses. The dispatch page is byte-identical to
the unchanged original packer. Its `vkMapMemory` call has six native arguments
and needs no PC RPC map adaptor. Borrowed device/instance identities and the
primary filesystem remain owned by the supervisor.

Run `node tools/sd-launcher/ps2-shadow-local.cjs`. It checks 24 selections and
allocation records against the original JS helper, the exact dispatch page,
18 ownership/failure/cancel cases, RX byte/mapping rejection and an actual host
RX trampoline that checks all six map arguments. Host mapped spans are real;
Switch kernel, Vulkan and GPU-object ownership boundaries are modeled. The
ARM64 preparation library is compiled and PIC checked, not executed. This
component is not yet connected to the PS2 application entry, video surfaces
or post-entry cleanup, and does not qualify standalone PS2 execution.

`av.c` constructs the unchanged 4 KiB paired AV description without a PC
packer: the original 288-byte AV prefix, primary 224-byte lease and 528-byte
pair tail at offset 512. Both source ranges remain distinct complete
4,198,400-byte allocations. Each has its own buffer, memory, descriptor
objects, per-image commands, acquire semaphore and per-image semaphores.
The original 960 x 540 transport, 1024 x 1024 maximum, 36 KiB AudioOut arena
and scanout scratch geometry are preserved. Opaque Vulkan handles are not
mistaken for virtual addresses.

`node tools/sd-launcher/ps2-av-local.cjs` compares every byte with the exact
unchanged R176 AV/pair/lease JS packers for 16 configurations and 1..8 images.
It runs the C producer as linked host code and actual host RX code. 3,148
invalid calls reject malformed, overlapping or aliased descriptions without
writing output. ARM64 is compiled and PIC checked; no GPU or engine entry
is performed. Exact reference shaders and helpers are pinned separately in
`av-profile.json`. This pure packer grants no allocation, mapping, execution
or cleanup authority. The controller still needs to create/admit the actual
GPU graphs, connect engine execution and validate its post-entry receipts.

`video.c` now creates those two GPU source graphs natively. It borrows the
current renderer's device, command pool, images and views, but separately owns
each source's complete 4,198,400-byte buffer/memory, descriptor objects,
graphics pipeline, shader modules, acquire semaphore, per-image semaphores
and command buffers. All mapped pages require fresh current-PID and scoped
GPU RW/opaque-attributes-0x60/zero-reference admission before CPU clearing.
The original coherent memory selection uses the current graphics snapshot
and actual buffer requirements; allocation size is exact. Clearing advances
in at most 64 KiB chunks. Each work unit performs at most one provider call,
mapping query or clear plus one identity check.

The unchanged display vertex/fragment shaders consume only binding 1. The
original compute module contains just its entry and return, so this factory
does not create an unused compute pipeline or bind a parent control buffer.
Preparation never acquires, submits or presents a frame. Before freeing any
prepared command graph, cancellation performs an actual QueueWaitIdle; failed
waits and unknown create/map/unmap/destroy/free outcomes retain ownership.
An already initialized/nonempty owner cannot be rebound by resetting its
state field. The AV producer copies sources from the actual ready factory,
not caller-supplied resource flags.

`sdk-video.c` borrows the bootstrap-qualified graphics table and verifies its
current executable mappings. It independently checks the exact original
vkGetInstanceProcAddr cut, resolves the two exact semaphore API names and
admits their returned RX spans in the current Vulkan image. No session address
is guessed and no PC verification request is sent. The binding is immutable
for one retained controller lifetime. Actual resolver success on Switch is
not yet qualified locally; rejection checks and a separate real host RX
trampoline cover admission failures, direct arguments and void-result handling.

Run `IZ_SD_HOST_GPU=1 node tools/sd-launcher/ps2-video-local.cjs` for its local
component check. It covers 25 ownership/cancel/error cases, all image counts
1..8, 12 binding rejections, ARM64 compile/PIC/link and two actual PC Vulkan
prepare/render/close cycles. Eight full 1920 x 1080 frames from both independent
sources match every expected pixel using the unchanged original SPIR-V.
The host GPU uses its real allocations, descriptors, pipelines, commands and
semaphores; Switch PID/mapping metadata and MapMemory2 ABI are modeled.

Entry intent changes release authority. This factory deliberately has no
post-entry release shortcut: neither QueueWaitIdle nor the outer worker join
proves that nested PS2 CPU/GS/DSP workers stopped using their buffers. Native
controller integration and actual engine-specific cleanup receipts are still
required. No standalone compatibility flag is enabled by these component checks.

`receipts.c` validates the normal current-image capsule natively: all 13 row
headers, exact image bindings, bounded JSON, VM/TLS/SD/AudioOut cleanup,
the fifth private native IOP worker, paired presentation fences and asynchronous
readback retirement. The separate 128-byte local-storage status must also be
retired and balanced. It rejects startup/runtime/pre-GS failures and unknown
records rather than authorizing cleanup from an incomplete record. The older
native-owner source used different image geometry, a 73 MiB JIT and one video
slot; its admission policy cannot be reused for this image.

Run `node tools/sd-launcher/ps2-receipts-local.cjs`. It compares 549 cases
against the unchanged current JS validators and executes both linked C and
actual host RX code under ASan/UBSan. ARM64 is compiled and PIC checked. Of
these cases, 94 are accepted; 54 valid general-JSON/reference inputs are
deliberately rejected by the stricter canonical emitter, size and normal-path
policy; no reference-rejected case is admitted. Duplicate keys, invalid UTF-8,
overflow and input/workspace/output aliases are covered. Historical actual
paired-GPU/IOP receipt rows remain untouched; their original old image header
is rejected. Positive current-address cases use explicitly modeled headers
and are not physical executions of R176.

These records alone prove no actual SDK join, current process or live mapping.
The controller must additionally validate its retained worker ticket, original
context/AV/descriptors, Main retirement, engine globals, guarded entry stack
and JIT mapping closure before granting each owner post-entry release. No
resource is freed and no standalone execution is enabled by this decoder.

`execution.c` now connects already prepared owners to the real ABI6 worker
and its post-entry retirement. Its opaque state lives in the retained parent
allocation beside the four resource owners, outside all published ABI pages.
It verifies the published startup pages against the unchanged packer, derives
the AV page from the ready video factory, admits the current data mapping and
walks the complete free JIT window before acquiring presentation and entering
the worker. Both guards contain the original byte `0x6d`.

STOP before entry cancels preparation without starting the engine. After
entry it writes the existing cooperative exit flag once and continues polling.
The controller calls the actual worker join; a synthetic JOINED poll state,
nonzero return, changed callbacks/PID/owners or an unknown result cannot grant
release. Following actual join, it freshly rechecks immutable context/AV/Main,
the guest-freed Main receipt, both guards, all current heap/TLS/stack/GS globals,
the current capsule and retired storage, and the complete unmapped JIT window.
Only that private state can open the four internal post-entry release paths.
Video first performs a real QueueWaitIdle and closes both graphs. Shadow,
child files and remaining SDK blocks follow; Main is never freed twice.
Presentation returns before image unload: the actual ABI6 image callback
refuses release while presentation is held. At this point the worker and both
GPU graphs have already closed. Unknown presentation release retains the
image without requesting its unload; unknown unload is never replayed.
These operations finish the same serialized state machine.
Each work unit makes at most one provider call and one fresh identity check.

Build this combined controller with `IZ_PS2_EXECUTION`; the ordinary standalone
preparation builds deliberately omit its internal retirement symbols. Run
`node tools/sd-launcher/ps2-execution-local.cjs` for its comprehensive check.
32 scenarios run both as linked C and an actual host RX image: six clean
completion/cancellation cases and 58 retained failure cases in total. Fixture
workers are real pthreads and their buffers are real allocations. The actual
unchanged supervisor worker begin/poll/join, presentation acquire/release and
image release/step callback bodies run in this check. SDK pthread calls, GPU,
IFile and the underlying image stepper boundary are modeled. An exact copy of
the previously qualified controller additionally reproduces its old `0xe40d`
release conflict against those callbacks. Both linked and actual host RX runs
reject that order explicitly and validate the corrected completion sequence.
ARM64 is compiled and PIC checked;
the original PS2 engine is not executed or changed. Retained fixture teardown
is explicitly separate from production cleanup authority.

`launch.c` now supplies publication and execution over those ready owners.
Its opaque storage lives entirely inside public `app.data`, alongside the
controller's stable provider contexts. It derives the original startup,
SD/save, paired AV, Vulkan dispatch and native HWGS ownership pages directly
from ready resource owners and the selected game/BIOS. It accepts no PC-packed
page. The HWGS inventory describes the memory component's actual five live
SDK blocks, with the dispatch at index 3; its execution-qualified flag stays 0.

The same retained span contains the 36 KiB AudioOut arena, 4,325,376-byte scanout
scratch, 32 KiB log ring and two 64 KiB file/save caches. All remain distinct from
provider state and FS IPC scratch. Each publication unit performs at most one
fresh mapping query or 64 KiB clear/copy, plus a current-process identity check.
Ordinary RW mappings with zero IPC/device references are required; opaque GPU
attributes are not inherited by these SDK buffers. Both entry stack guards
are published with the original 0x6d byte. Execution starts only after all pages
are published, using the existing real worker and nested cleanup proof.

STOP during partial publication enters the existing pre-entry cancellation
path without reading incomplete startup pages or entering the engine. A lost
process identity, invalid mapping, changed owner or unknown outcome retains
the whole controller and admits no retry. Begin rejection takes no resources.
Public app.data binding, buffer/provider aliases and child-owner overlap are
checked before publication can overwrite any buffer.

Run `node tools/sd-launcher/ps2-launch-local.cjs`. It exercises 64 lifecycle
scenarios as linked C and actual host RX code through unchanged supervisor
callback bodies, 10 publication fault/cancel cases and 28 invalid begin calls.
Two published captures match every original startup/SD/save/AV/dispatch/HWGS
byte, including zero-filled auxiliary buffers initially dirtied by the test.
ARM64 is compiled and PIC checked. SDK/GPU/FS and the underlying image-stepper
boundaries are modeled; the original engine is not entered or changed.
The first failed check exposed a test harness call through an already unmapped
host RX image; its log is preserved and the harness now uses its live linked
entry for subsequent publication fault cases.

Still required: the native SD application must prepare the ready resources,
choose a free JIT window, display the game picker and connect INIT/FRAME/STOP
to this launch controller. The normal-only completion policy retains
unsupported engine failures. Current image/profile packaging, actual Switch
qualification and standalone PS2 compatibility remain incomplete.
