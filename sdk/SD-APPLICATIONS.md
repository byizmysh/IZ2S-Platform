# Autonomous SD applications (experimental)

Both console shells have an **experimental SD application mode**. Select
**Read applications from the console SD card** before starting a fresh session.
Initial native session acquisition and menu preparation still require the PC.
After that preparation, a console CPU thread owns directory discovery, file
reads, shader/pipeline creation, input, presentation and application transitions.
Host polling supplies telemetry only. On 7 November 2026, Switch 2 executed
an ARM64 module read from SD, verified its persisted result, repeated the
launch through the SD supervisor and switched to a GPU application from SD.
On the later physical trial, the native progress application completed and returned to the menu offline; the GPU ring displayed and returned too. Its animation bug and BASIC Direct input sounds were recorded separately.

## Files on the card

```text
SD:/IZ2S/Homebrew/Modules/<module.id>/launcher.izapp
SD:/IZ2S/Homebrew/Modules/<module.id>/asset-<sha256>.izdata
```

`launcher.izapp` is a prebuilt application package, not the PC `.izm` archive.
It contains the initial state, data, uncompressed SPIR-V, optional private
memory, a menu tile and the pad-normalization shader. Native packages also
carry PIC ARM64 code and its initial mutable data. Package version 2 keeps
native code separate from repeated compute shaders and supports the explicitly
declared 128 MiB coherent GPU allocation profile. Version 1 packages remain
readable. The console validates
the fixed header, bounded section ranges and each section's CRC32. It creates
pipelines on demand; application pipelines are not prepared on the PC at boot.

The native thread opens children of the existing promoted SD filesystem.
It does not depend on `MountSdCard`, FTP, an HTTP file server or a PC callback
for individual reads. The SD capability is borrowed until the thread joins.

The menu reads the module directories when the supervisor starts. The menu's
existing refresh action also rescans the card. Installing from the panel
requests a rescan at the next menu boundary. A runs the selected application;
two distinct R3 presses within 600 ms return to the menu. B also returns when
the application uses the standard SDK `hbExit()` behavior. Manual recovery uses
the separate SD application **Restore connection** (`iz-reconnect`). Plus has no
recovery binding: the user reports that pressing it disconnects the console.

Up to 15 supported applications are listed, within at most 128 module folders.
Additional or invalid packages are counted as rejected. One application's
pipelines are cached; switching to another releases those resources after
the previous queue is idle. The permanent staging arena is 512 KiB. Small
executable, shader and control sections use that arena or a larger bounded
temporary allocation. Large private images stream directly from SD into their
final coherent GPU allocation, without a second complete CPU copy. Section
CRC32 is accumulated from each bounded IPC scratch chunk before copying it
into that allocation. Native entry waits for the complete checksum; the CPU
does not reread the whole GPU mapping to verify the SD file. Packages
are limited to 48 MiB for version 1 or 80 MiB for version 2. Private GPU memory
normally has a 32 MiB limit; the declared
`resident-audio-128` profile permits up to 128 MiB. These are admission limits,
not guarantees that an allocation will succeed. The native stack is 1 MiB.

## Native entry

The application descriptor can include:

```json
"native": {
  "format": "iz2s.native-app",
  "abi": 1,
  "code": "native/native.bin",
  "initial": "native/data.bin",
  "dataBytes": 4096
}
```

Compile code with `tools/sd-launcher/build-native.cjs`. Its entry is
`uint32_t iz_app(IzNativeApp *, uint32_t event)` from
`core/console/sd-launcher/native/native-app.h`. Events are INIT, FRAME and
STOP. Writable state lives in `app->data`; the builder rejects writable
globals and relocations. Callbacks provide module-relative SD reads,
verified SD writes, buttons and return to the menu. A successful STOP must
mean that the module has stopped using every resource it owns.

Native ABI 2 preserves the 112-byte ABI 1 prefix and extends it to 152 bytes
(`IzNativeApp2`). It additionally borrows this application's coherent private
GPU mapping, clock/sleep callbacks and an optional fixed AudioOut service table.
The bootstrap verifies every SDK function body before binding that table.
`native.capabilities: ["audio-out"]` requests it. Native ABI 2 can accompany
the repeated compute sequence; native code/data have their own package sections.
Audio STOP must drain every outstanding DSP descriptor before the supervisor
can release or reuse private storage. Unknown ownership retains the allocation.

The console reads the code file, allocates its source and data, maps an RX
alias using the retained RO service, calls the entry and unmaps it after
STOP. These transitions require no JS interpreter or per-module PC upload.
The initial supervisor itself is still bootstrapped from the PC.

## Native engine images (ABI 3)

Native ABI 3 preserves the 152-byte ABI 2 prefix and extends it to 168 bytes
(`IzNativeApp3`). Declare `native.abi: 3`; package version 2 flag 16 selects this
ABI explicitly. Earlier packages keep their ABI and section bytes. Old
supervisors reject the new flag rather than passing a shorter callback table.
`build-native.cjs` accepts `{abi:3}` when used as a library.

The added `load_image(app, relativePath, receipt)` callback reads an `IZNATRL1`
companion, chooses a current self-process code window, allocates final owned
code/data, streams and verifies its sections, applies bounded fixups, closes the
file and creates RX aliases. It does not invoke the engine. A successful
64-byte `IzNativeImage` receipt contains version/size, code/data pointers and
extents, entry/spawn pointers and an instance/generation-bound token. There is
one live image per app. A second load is rejected without overwriting the
existing receipt. This ABI 3 callback remains synchronous. ABI 4 below provides
the incremental loading and cleanup path for larger images.

The module constructs the engine's actual startup context and may invoke its
entry itself. This callback does not supply PS2 filesystem/JIT/AV/PAD contexts,
start workers or grant renderer ownership. Those remain the module's job.
After joining every engine user and validating real cleanup receipts, call
`unload_image(app, token)` before returning zero from STOP. Wrong, stale or
duplicate tokens are rejected without SDK calls. Load/unload callbacks belong
to the serialized owning INIT/FRAME/STOP and must not be called from workers.
The receipt is not a general-purpose thread-safe handle.

CRC/missing-file failures clean up owned allocations. Unknown map/unmap
outcomes retain the entire parent and child. If STOP leaves a live image, the
supervisor also retains the app's code, data and image state; it does not infer
that engine users stopped merely because STOP returned zero.

`tests/sd-launcher/native-image-local.cjs` executes an actual host RX parent,
which loads a second RX image from modeled SD, fixes its internal helper
pointer, invokes it and repeats with new addresses and tokens. It also checks
reload within one app, stale tokens, missing/CRC failures, lost map reply,
rejected unmap and a live image left at STOP. With `IZ_SD_COMPANION_IMAGE`, the
same public callbacks load/inspect/unload the complete PS2 image without
entering its ARM code. The report is
`data/diagnostics/sd-native-image-local.json`. Parent ARM64 compilation/PIC and
linked supervisor checks pass; console FS/RO/self SVC/allocator boundaries are
modeled. Physical execution of ABI 3 remains unqualified.

## Incremental engine images (ABI 4)

Declare `native.abi: 4` to select the 192-byte `IzNativeApp4` table. It preserves
all three previous prefixes and adds `image_begin`, `image_step` and
`image_release`. Package version 2 uses flag 32 together with native flag 1
and image flag 16; companion files also set flag 8. Old supervisors reject
these flags. The native builder accepts `{abi:4}`.

`image_begin(app, relativePath, &ticket)` reserves the app's one image job.
It may allocate the small owner state, but performs no file or RO calls.
`image_step(app, ticket, budget, &progress, &image)` advances between 1 and 64
work units. The 72-byte progress record reports state, internal stage, error,
completion, bytes read and cleared, mapped pages and accumulated work units.
The 64-byte image receipt becomes available only in READY. Tickets remain
bound to the owning app instance and generation.

A work unit reads or clears at most 64 KiB, applies one bounded fixup batch,
queries one memory region, maps/unmaps one page, or performs another bounded
ownership action. Capturing self-process metadata uses a fixed group of ten
read-only SVC calls. These are operation bounds, not a wall-clock deadline:
an individual allocator or SDK call cannot be preempted by the loader.
CRC and relocation state survive between steps, even when the shared scratch
buffer is used by other supervisor work.

After joining every engine user, call `image_release(app, ticket)` and keep
stepping until CLOSED or FAILED. Release also cancels an unfinished load.
It immediately withdraws the executable receipt, then closes the file,
unmaps confirmed pages in reverse order and frees verified allocations in
later steps. Cancellation during allocation validation finishes checking
ownership before freeing anything. Unknown results retain the image and its
parent rather than retrying an uncertain operation.

An ABI 4 STOP handler may return `IZ_NATIVE_STOP_PENDING` while stepping this
cleanup. The supervisor continues input and presentation, calls STOP again
on the next frame and makes no further FRAME callbacks. Only a successful
final STOP with no live image permits returning to the menu. Both ordinary
completion and double-R3 cancellation use this path.

Local checks cover fourteen small-image incremental cases and six complete
supervisor cases: ordinary load/return, cancellation during reading and
cancellation during partial mapping, each followed by another launch. The
full supervisor tests inspect both a host engine and the real PS2 image;
only the host engine is entered. See
`data/diagnostics/sd-cooperative-native-local.json` and the cooperative section
of `data/diagnostics/sd-engine-loader-local.json`. These tests model the kernel
and device boundaries; ABI 4 has not yet been qualified on Switch.

This incremental interface applies to companion engine images. Initial
`launcher.izapp` staging, pipeline creation and its large private GPU section
still use the existing synchronous startup path. PS2's actual startup
context, workers, JIT, video and cleanup contract also remain separate work.

## Companion files and loading into RAM

An ABI 2, 3 or 4 application can declare up to 16 companion files, each at most
128 MiB and at most 512 MiB in total. They can hold a native engine, audio,
game data or another application-owned format. For example:

```json
"native": {
  "format": "iz2s.native-app",
  "abi": 2,
  "code": "native/native.bin",
  "dataBytes": 4096,
  "assets": [{
    "file": "native/engine.izrel",
    "path": "asset-<64 lowercase SHA256 characters>.izdata",
    "bytes": 44089496,
    "sha256": "<64 lowercase SHA256 characters>"
  }]
}
```

Replace both placeholders with the actual SHA256 and set the exact file size.
The source stays inside the app directory. Its flat target name contains that
same hash. The builder and package inspector verify the complete source.
Package version 2 flag 8 binds the exact companion identities into the launcher
header: offsets 160/164 contain count/total bytes and offsets 368–399 contain
their canonical SHA256. The digest is SHA256 over UTF-8
`iz2s.sd-assets.v1\n` followed by compact JSON of `[path, bytes, sha256]` rows,
sorted by path. Other reserved fields stay zero. Packages without companions
keep their previous bytes. The installer rejects a missing, extra, changed or
misidentified companion list before its first SD call.

Build output contains `launcher.izapp`, its `asset-*.izdata` files and
`sd-bundle.json`. Keep them together for the installation command. When copying
with a card reader, copy the launcher and all companions into the module folder;
the console does not need the bundle manifest. A companion's native relative
read path is its exact `asset-*.izdata` filename.

The installer writes each new blob to a unique temporary file, checks its
source while writing, flushes/closes it, reads it back completely, then renames
and commits it. Only after every companion is verified does it publish the
launcher. Existing blobs with the same hash are fully verified and reused.
Corrupt blobs are reported without replacement. Updates retain old blobs so
an older or running application can finish using them. Unknown transfer results
fence later operations; they are never retried automatically. The normal SD job
requires verified companion receipts as well as launcher verification before
requesting the menu rescan.

An app can read a companion into its own RAM during INIT and then use that
memory throughout FRAME. This avoids repeated SD reads; allocation limits and
cleanup ownership still apply. A companion is not executed merely by copying
it: a native engine image additionally needs relocation, executable mapping and
its own startup contract. The PS2 image loader described below is still separate
from that startup integration.

`tests/sd-launcher/companion-local.cjs` exercises the normal queue and installer,
then feeds their installed files into the production C supervisor and native
loader. The actual host RX application reads a 196625-byte companion into RAM,
executes 48 frames across two launches with no further file reads during FRAME,
and returns to the menu. Missing and corrupt companions also return cleanly.
The same entry compiles and passes PIC checks for ARM64. Only the host version
is executed; FS, RO and Vulkan boundaries are modeled. This is local evidence,
not Switch qualification.

A separate local registry round trip packs a real `.izm`, inspects and extracts
it for each shell, builds the SD bundle and transfers it through the production
installer against a modeled SD device. A valid archive containing a companion
whose bytes disagree with its declaration is rejected. Evidence is in
`data/diagnostics/sd-companion-registry-local.json`.

## Build and install

Use a local compiler toolchain already present on the machine. These commands
do not download dependencies:

```sh
node core/console/sd-launcher/build.cjs
node tools/sd-launcher/app.cjs build /absolute/path/to/app /absolute/path/to/output
node tools/sd-launcher/app.cjs inspect /absolute/path/to/output/launcher.izapp
node tools/sd-launcher/app.cjs install launcher-SESSION /absolute/path/to/output/launcher.izapp
```

Alternatively, the panel's **Install on SD** action prepares a compatible
installed `.izm` package and writes its `launcher.izapp` once. The transfer
runs as a durable SD installation job, reports write/readback progress, and waits for the native catalog to acknowledge the application. The menu inventory comes from the console, including packages copied with a card reader; a PC transfer receipt alone does not mark a package ready. Temporary PC build files are removed after transfer. It uses the PS2 large-block approach: verified upload buffers, flush, close,
complete readback, then rename to the final filename and commit. An existing
version stays at its final name until the upload and readback have succeeded.
During replacement a unique backup retains the previous version through
publication. An uncertain operation is not replayed or automatically rolled
back. Keep any `.bak` file after an interrupted publication for recovery.

Copying the generated launcher and every companion to the same folder with a
card reader is also supported. No PC package or server directory is needed on
the card.

## Selecting PC packages from the SD menu

The ordinary SD runtime publishes compatible PC packages to the existing X
picker in both shells. Selecting one creates a console-owned request to install
its exact package SHA256 on SD. The picker returns home while the request waits;
SD applications can still launch and return. The request queue survives network
loss within the same console process and has no elapsed-time expiry. The current
menu holds at most 16 distinct pending selections. Repeated selections of the
same pending version share a request.

PC catalog publications use a separate 2 MiB + 4 KiB staging area. `IZSDSEL1`
contains a 512-byte header, at most sixteen 96-byte ID/hash/tile-CRC records and
their 128 KiB tiles. The native worker validates the complete publication before
copying it into menu storage at a frame boundary. A selection binds to the
previously visible catalog before applying a newer publication in the same
frame. Apps running in the foreground defer catalog publication. Invalid
previews preserve the last accepted catalog and the running SD application.

The server persists a session/request key in the installation job and a local
receipt before acknowledging the console. A completed key cannot create another
installation after a host restart or a repeated observation. A changed PC
package is rejected instead of installing another version. Acknowledgement waits
until the file passes readback and the native SD catalog accepts it, or a known
failure is recorded. Uncertain writes retain the request and fence later work in
the same console process. Queue and receipt persistence covers host-process
restart; physical power-loss durability has not been qualified.

Both menu shaders also protect their state with checkpoints. Native catalog
edits and app returns advance the existing edit epoch, so a valid new catalog
is sealed instead of being replaced by an old checkpoint. The saved return
header clears the launch transition to avoid launching the application again.
Headless PC Vulkan tests execute each packaged shell's actual menu SPIR-V for
X/A selection, request capture, a deferred catalog update and app return. Local
negative controls reproduce failure when either the epoch update or transition
clear is removed. This is PC GPU evidence, not Switch hardware qualification.
Enable it with `IZ_SD_HOST_GPU=1 node tools/sd-launcher/check.cjs` on a host with
Vulkan headers, loader and a working local GPU.

Only packages with an autonomous SD entry are offered. The real current PC
inventory exposes the experimental IZDOOM adapter in this picker. PS2 and other
host-dependent modules still require their own migration; unsupported packages
are not converted to runnable icons.

## Current compatibility boundary

The implemented runtime accepts standalone GPU ABI 1 applications, including
optional private memory, repeated compute sequences and the native entry
above. Empty SDK capability
lists are allowed. Applications requiring host SDK services, shell extensions,
or audio providers are rejected with an explicit reason.

**The currently installed PS2 package is not migrated.** Its `hostRuntime`
performs module-specific preparation on the PC. Copying its GPU launcher tile
would not load its native engine. A native SD entry and a console-owned
initialization contract remain necessary. PC streaming applications also need
their PC producer.

The PS2 migration now has a separate prelinked ARM64 engine-image format,
`IZNATRL1`. Its 256-byte header describes initialized RX/RW bytes, zero-filled
memory extents, source bases, section CRCs and sorted 32-byte fixups. The C
rebaser processes at most 64 KiB of records per call and checks bounds, original
operands, duplicate locations and ARM displacement limits. The caller must
validate all section CRCs and finish before mapping executable pages. ELF
export checks every allocated relocation, static GOT contents and the unwind
header; unknown cases fail instead of inferring pointers from arbitrary data.

The exact installed PS2 R110 image has been exported locally without invoking
its per-session relinker or reading its external build dependencies. Its
44,089,496-byte engine image carries 97,176 fixups. Local C tests preserve its
original code/data byte for byte, rebase it, decode the resulting targets and
restore every original byte on a round trip. Three smaller cases match fresh
independent LLVM links, including a data segment below code. The rebaser also
compiles for ARM64 and passes the native PIC check. Reports are
`data/diagnostics/sd-relocatable-native-local.json` for the linker oracle and
`data/diagnostics/sd-relocatable-ps2-local.json` for the real engine image.
`python3 tests/sd-launcher/relocatable-local.py` runs the self-contained oracle;
add `--ps2-elf` and `--ps2-native-report` for the actual qualified local inputs.
This engine image is not an IZ application entry. It is not offered as a
runnable PS2 package: native startup/context construction, JIT/Main and thread
ownership, video handoff and console qualification remain to be implemented.

The internal C image loader (`relocatable/loader.c`) now reads this format
through the production native filesystem parser. It allocates the final owned
code source and data, checks their current Normal/RW memory properties, reads
and checks each section using 64 KiB of existing IPC scratch, applies fixups
in bounded batches, closes the file and then creates the RX aliases. Checksums
and fixup completion precede the first executable mapping. It does not allocate
a second whole engine image or a whole fixup table. Passing a NULL address plan
now selects the code window locally. The planner captures the current self
process ID and GetInfo ASLR/alias/heap/stack ranges, then walks current
QueryMemory regions. It excludes all three reserved ranges, rejects invalid
or overflowing metadata and limits the walk to 1024 regions. No address is read
from a package, previous session or host callback. A supplied owner plan is
independently checked against the same current ranges and free pages.

After reading/fixing the image and closing its file, the loader captures these
ranges again and rechecks the complete destination before its first RX map.
A new reservation or occupied region rejects the load and releases its owned
source/data without starting executable mappings. The retained RO service also
checks process identity and destination pages, and kernel map rejection still
rolls back preceding confirmed mappings. Planning does not reserve an address;
the owner must serialize other mappings and handle races. Self SVC wrappers are
derived from the bundled RO payload's existing GetProcessId/GetInfo/QueryMemory
register conventions. They are ARM64 compiled/linked/PIC checked locally; their
actual execution in SmileBASIC remains unqualified. The native app bridge is
available through ABI 3 and the incremental ABI 4 above; the PS2 startup
contract remains unfinished.

The loader supports cooperative cancellation and refuses reentrant or duplicate
loads. Known map rejection and confirmed protection rollback release preceding
pages and allocations. Lost SDK replies, wrong process identity, failed
protection rollback or unmap rejection retain the sources/mappings and fence
further operations. It never retries an uncertain SDK call. An owner must join
every engine thread/user and validate actual cleanup receipts before unloading
an engine it has entered; loading alone grants no VM cleanup proof.

`python3 tests/sd-launcher/image-loader-local.py` runs the small engine against
the production filesystem/loader/rebaser with modeled SDK calls and real host
shared RW/RX mappings. Its loaded bytes match a fresh LLVM link at the actual
allocated addresses. Twenty-one cases cover reload, CRC/short reads, cancellation,
allocation/map failures, confirmed/failed protection rollback, lost replies,
identity rejection, reentrancy, automatic selection with different code bases,
and a reservation/occupied range appearing before map. Both the supplied-plan
and automatic-plan loaded bytes match fresh LLVM links at their actual final
addresses. Twenty-three additional planner cases cover fresh metadata,
reserved-range exclusion, fragmentation, bounds, identity, cancellation and
read-only kernel errors. Their report is `data/diagnostics/sd-address-local.json`.

Adding `--engine-image` and `--engine-report` checks six cases with the real PS2
export, including two automatic loads with different code and data addresses,
all decoded fixups, zeroed BSS, RX alias contents, table-CRC rejection, read
cancellation and late reservation/occupation rejection. These combine with the
small image's twenty-one cases for twenty-seven loader cases. Reports are
`data/diagnostics/sd-engine-loader-local.json` and
`data/diagnostics/sd-engine-loader-ps2-local.json`. ARM64 compilation/PIC checks
also pass for the linked loader, planner and self SVC wrappers. Tests model the
kernel/RO boundaries, never enter ARM engine code and never contact the console.

The exact installed **IZDOOM 0.18.2** GPU build now has an experimental native
SD adapter. Original game, state, shader and music-bank bytes must match their
pinned hashes. The package contains the complete GPU program/data and a 1948-byte
PIC entry replacing its finite host audio startup/close controller. The GPU
prepares PCM in its own coherent mapping; native FRAME performs the two startup
handshakes, opens AudioOut and appends the existing 32 descriptors. Playing
requires no host callback or PCM copy. STOP stops, flushes and drains those
descriptors before returning memory. The inherited mutable-PCM/finite-queue
audio design remains experimental. Local production-code tests cover startup,
autonomous play, repeat launch, delayed releases and unknown ownership;
physical sound and gameplay acceptance are recorded separately.

An integrated local model also runs the production supervisor, filesystem and
native loader with the actual adapter compiled for the host CPU and mapped RX.
It exercises ABI 2, repeated GPU compute, the private-memory/service-table
handoff, startup, menu return and a second launch with one cached GPU allocation.
Modeled AudioOut asserts that all descriptors are returned and the device is
closed before native code/data or private GPU storage are released. Vulkan and
the audio device are simulated in that test; it does not prove physical sound.

A separate local test now executes the complete installed IZDOOM program on a
real PC Vulkan GPU through the production supervisor. The fixture changes the
package ID and replaces the ARM entry with the same controller compiled for the
host CPU. Game shaders, initial state, data, private image, tile and pad shader
match the ARM package byte for byte. The test streams the 65,766,480-byte image
to its final 68 MiB GPU allocation, generates nonzero PCM, advances the game
without VM faults, captures a complete scene, drains the modeled audio queue,
returns to the menu and launches again. Staging remains 372,308 bytes. The FS
capability and AudioOut boundaries are modeled; no console or sound device is
accessed. Run it with `node tools/sd-launcher/doom-local.cjs` on Linux x86_64
with the existing local Vulkan/clang toolchain. The report and screenshot are
saved under `data/diagnostics/sd-doom-gpu-local.json` and
`data/build-cache/sd-doom-gpu/frame.png`.

On the local RX 5700 XT, moving CRC verification into the SD read stream reduced
this two-launch test from 21.149 to 8.076 seconds, with the same package and
rendered frame. The old post-read verification alone spent about 7.1 seconds
per launch reading uncached coherent GPU memory. These are PC test timings,
not a Switch speed estimate. Production-code tests also reject a corrupt final
byte and a short final read of a large private section before app entry, while
closing owned file handles and releasing unused GPU storage.

The first physical IZDOOM trial on launcher-R1nEKO closed SmileBASIC with a
system error. The package write and full readback had succeeded. The last
retained observation showed approximately 44 MiB read and no native entry yet;
it is not a crash dump and does not prove the exact cause or exclude later
progress. See `data/sessions/launcher-R1nEKO/sd-izdoom-hardware.json`.
The loader subsequently removed whole-package CPU staging and fixed a version-2
compute-stage selection bug. A production C model now loads a 65 MiB private
image directly into a 68 MiB modeled GPU mapping with only 512 KiB of staging.
These changes still require fresh hardware qualification. A separate read-only
CRC probe isolates large SD reads before trying GPU allocation, native entry
or AudioOut again:

```sh
node tools/sd-launcher/large-read.cjs launcher-SESSION izdoom-prime18
node tools/sd-launcher/large-read.cjs launcher-SESSION izdoom-prime18 --gpu
```

The first probe has a 64 KiB CPU sink and creates no GPU allocation. The second
reads into a private coherent GPU mapping but creates no app pipeline, executes
no app entry and never opens AudioOut. Both verify the complete private-section
CRC and join before releasing resources. After those pass, the interactive
IZDOOM trial can use `--installed` to avoid rewriting the already installed
package; it checks the card's header against the prepared package, and the
native launcher validates every section CRC before native entry.

This mode does not boot after a console power cycle without the initial
SmileBASIC/native bootstrap. Existing normal launcher mode remains available.
The experimental mode is opt-in until hardware qualification.

## Checks and hardware acceptance

```sh
node tools/sd-launcher/check.cjs
node tools/sd-launcher/demo.cjs
node tools/sd-launcher/native-demo.cjs
node tools/sd-launcher/probe.cjs launcher-SESSION
node tools/sd-launcher/native-probe.cjs launcher-SESSION
node tools/sd-launcher/hardware.cjs launcher-SESSION
```

The local check executes the production C supervisor against modeled FS/Vulkan
boundaries with ASan/UBSan. It covers two applications, private storage,
repeated compute, return and re-entry, rescan, corrupt data, short reads,
unexpected handles, lost transport and native write/read/rename/delete.
It also runs the production native loader against real host RX mappings,
executes INIT/120 FRAME/STOP twice and checks the written receipt.
This is not evidence of execution on Switch 2.

The actual Switch 2 evidence is stored separately:
`data/sessions/launcher-k1yrhO/sd-native-execution-2576483637.json` and
`data/sessions/launcher-k1yrhO/sd-hardware-test.json`. The first verified
528 code bytes loaded from SD and a 120-frame result of `0x4b92e586`; no
module image was uploaded during launch. The second verified two native
launches, a GPU launch, returns and joined cleanup. These were tests of the
IZPSlauncher session; the other shell still requires its own observations.

An earlier experiment hung and lost the console link while another native
module was active. The precise cause is unproved. Every experiment now
requires a free launcher, reserves the application slot and joins the
original renderer before allocation, filesystem access or mapping. Both
probes passed at that boundary. Never replay an uncertain native operation
or reuse the disconnected session.

The first physical-input trial was stopped before network disconnection:
the user reported an unresponsive menu. Read-only diagnostics showed the
supervisor still presenting 60 frames/s. The retained host pad sweep had
sequence 1 and was overwriting the fresh primary sample in the shaders.
The updated image scans handheld and all eight player slots and disables
that frozen host header before every submission, including after menu
snapshot restores. Local coverage checks fresh merged input and the stale
header boundary. The user subsequently confirmed that selection and both launches worked on launcher-mOkqZ0, then confirmed offline native progress/return and GPU display/return. See that session's sd-acceptance.json. The original GPU demonstration omitted its own frame-counter update; the corrected package adds a visibly orbiting point and an A color toggle.

For physical acceptance, start a fresh console session with FS ready, run the
short native write probe, install both generated demonstration packages and
start an SD session in each shell. Open an application, return and open the
other. Then disconnect the PC link and repeat all three transitions using
physical controls. Also check a card refresh and a normal joined shutdown.
Record physical observations separately from host telemetry.

The reference route follows the PS2 filesystem implementation and ownership
rules, the PS1 resident-menu return design, the provided Switch 1/Switch 2
catalogue and the local NS2 function/audit reports. Symbol availability alone
is not a runtime ABI qualification.

## BASIC input sounds and native recovery

The ordinary bootstrap returns from RPC to BASIC Direct when Recv loses the
PC socket. The console-native SD renderer continues, while Direct consumes
controller navigation and plays its key feedback. The derived **NS2SD** source
keeps BASIC inside RUN in a SleepThread loop after disconnect, without changing
process audio volume or drawing VSYNC frames over the native renderer. It carries
a reserved greeting marker; SD startup verifies that marker before installing
workers. Original upstream source stays intact. This correction is locally
validated; this derived BASIC source still requires its own physical sound test.

Every SD menu now requires the recovery worker, including hardware harnesses
attached to an ordinary launcher. `core/console/recovery/prepare-sd.cjs` runs
before the SD supervisor obtains its filesystem or allocates its working memory.
It qualifies suspend/resume against the new auxiliary thread, enables BASIC
foreground parking before authenticated handover, and verifies the actual parked
state through the new channel. An enabled network connection alone is insufficient.
Existing verified owners are reused; incomplete installations are retained and
never redeployed blindly. `sd-offline-ready.json` records the successful boundary.
Read-only and native-only probes do not start an interactive SD menu and retain
their separate qualification path.

The experimental recovery worker has its own wildcard TCP listener, authenticated
receipts, console address announcements, and no overall offline deadline. Host
bridge requests remain serialized through a stable connection interface; panel
status and Restore connection bypass a blocked native request. Application and
filesystem state must be reconciled before admitting new work. The worker can
park the separately identified BASIC interpreter thread during a healthy
authenticated handover for an existing ordinary bootstrap. The mandatory SD
preparation performs the auxiliary-thread suspend/resume check in that session.

On the RUN22 physical trial, the large IZDOOM section passed both bounded CPU
reading and direct coherent-GPU reading, then the native SD game started,
returned and started again. The user confirmed gameplay. Removing LAN restored
BASIC input sounds and closed the legacy control host: that harness had bypassed
recovery/foreground preparation. The mandatory preparation barrier fixes that
omission locally; a fresh physical outage test remains required. See
`data/diagnostics/sd-run22-hardware.json`. This trial does not qualify physical
LAN/Wi-Fi recovery or explicitly establish audible music or offline game return.

Signed address announcements also trigger a fresh authenticated route probe when
the old TCP socket is still open but silent. A new LAN/Wi-Fi endpoint, or a
same-address announcement after more than five seconds of silence, can therefore
restore the route before an outstanding SDK read reaches its timeout. Restore
connection forces the same probe without waiting for an announcement. The first
announcement that merely identifies an existing address as LAN/Wi-Fi does not
replace its healthy socket. Probes are serialized, send no SDK request and leave
the old socket in place until the fresh challenge and receipt succeed. Receipt
reconciliation then runs in the ordinary command loop: a cached response is
retrieved, and an executing operation stays pending without re-execution. Local
TCP tests deliberately hide the old route's FIN and verify both automatic address
changes and same-IP returns through the production host manager and C server.
These route tests do not qualify physical LAN/Wi-Fi behavior.

The **Restore connection** SD application increments a resident request counter
through the supervisor. Its native INIT writes `IZ_NATIVE_CONNECTION_REQUEST`
to shared-header byte 1008; the supervisor consumes that word once, then wakes
the independent recovery worker. The app returns to the menu after 60 frames.
It makes no network call, starts no worker and never uses Plus. Initial SD
startup installs its verified package when native recovery is enabled and a
menu slot is available; an identical card copy is read and reused. A full menu
preserves user applications and reports that the recovery tile could not be
added. The standalone `.izm` is built locally by `build-reconnect.cjs`.

The recovery worker repeats that counter in every signed address announcement;
the host retains the request until a fresh route authenticates, including when
the first probe fails. An offline request is still visible when communication
returns. This
also covers a silent TCP route while ordinary UDP beacons continue without a gap.
Within the existing 32-byte discovery payload, offset 24 contains the unsigned
32-bit wake counter and offset 28 contains feature bit 0 for that counter. Legacy
all-zero extension fields are still accepted; unknown feature bits are rejected.
The production C worker's emitted packets and the host's failed-first-probe retry
are locally tested. The supervisor also executes the real host RX version of the
native recovery action, returns and relaunches while Plus stays pressed in its
modeled input; only the explicit app action increments the counter. The supervisor
and worker images have changed since the physical short-outage trial and need
separate hardware qualification. These models do not establish why Plus closes
the physical connection.

The local RPC client's source remains byte-for-byte compatible with existing
PS2 packages, and its native packet protocol remains 1. The outer recovery
transport is recorded separately as `controlProtocol: 3`. While recovery is
enabled, the local HTTP bridge sends interim 102 Processing responses every
30 seconds for pending requests. This renews socket inactivity without returning
an SDK result or committing the final HTTP status. The same request can later
finish with a successful result or a normal error; it is never replayed by this
keepalive. Local tests use the unchanged client over real HTTP with a shortened
socket timeout and verify both final outcomes.

The first recovery worker crashed SmileBASIC on launcher-IrEODQ. Investigation
found six R_AARCH64_ABS64 function pointers in a compiler-generated callback
table. Static linking at address zero removed their relocation records, but
did not make those pointers position independent. Callback binding now uses
runtime PC-relative addresses. All raw native builders reject absolute and GOT
relocations in object files before linking. Final-ELF relocation checks alone
are insufficient. The corrected worker passed staged SDK qualification and
authenticated handover on launcher-R1nEKO. A separate auxiliary thread was
suspended and resumed successfully before parking the BASIC interpreter.

The hardware harness now qualifies startup, current thread, clock, NIFM status,
IP address, socket, nonblocking flags, bind/listen, poll and UDP announcement as
separate steps before enabling the full authenticated server or foreground
parking. An uncertain console result is never replayed.

On launcher-R1nEKO the user disconnected the network and confirmed that input
feedback sounds were absent and the GPU point animated. After the network
returned, the same session reauthenticated and a native SDK call succeeded;
no launcher restart was needed. This exercised the native BASIC-thread parking
path, rather than the derived NS2SD source. Evidence is recorded separately in
`data/sessions/launcher-R1nEKO/native-recovery-hardware.json` and
`data/sessions/launcher-R1nEKO/sd-acceptance.json`. Physical LAN/Wi-Fi changes,
DHCP changes and an hour offline remain unqualified.

The interactive hardware proof harness contains only installed SD packages.
The ordinary SD runtime adds the compatible PC picker described above. Closing
the trial stops and joins its worker,
releases its display reservation and resumes the ordinary launcher without
stopping the resident recovery service. The trial was closed this way on
launcher-R1nEKO; the original renderer resumed and the panel reported connected.
