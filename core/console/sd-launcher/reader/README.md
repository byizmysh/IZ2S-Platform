# Local Book Reader controller

The launcher supplies an ABI 2 native controller for the exact Book Reader
build pinned in `profile.json`. It does not alter the module, GPU programs,
font atlas or engine. Both IZlauncher and IZPSlauncher use `adapters.cjs`.

Installing Book Reader on SD exports the existing imported TXT/EPUB library
from the module's private PC data directory. The installer normalizes text
once, stages immutable SHA256-bound companion files and includes the controller
and library index in the SD package. Runtime JavaScript and the PC book store
are not needed for reading, pagination, appearance settings or saving progress.
New PC imports require reinstalling Book Reader on SD. An existing managed
installation also needs reinstalling to receive this native controller.

Installed directory: `SD:/IZ2S/Homebrew/Modules/book-reader/`.
`launcher.izapp` and `asset-<sha256>.izdata` are replaceable installation files.
`reader-state.bin` holds preferences and positions matched by book identity;
ordinary module removal preserves this user file. SD progress is authoritative
on later SD launches; reinstalling does not silently overwrite it from the PC.

Each native frame performs at most one 64 KiB read, 4096 layout characters,
64 KiB of line metadata publication, or 1024 page records. GPU library tables
remain stable while a new layout is built. Only a finished verified layout is
published for reading. Resident page turns use the original GPU decoder.
Progress writes are debounced; failed writes are retried after 300 frames.
STOP flushes remaining progress and is idempotent. No workers or devices are
owned by this controller. The supervisor retains uncertain FS outcomes.

Limits follow the original reader: English printable text, 128 books, 8 MiB
text per book, a 6 MiB GPU data budget and at most 512 MiB companion data.
Known book read/format/save errors use the original app error toast and keep
the library usable. Parent launcher exit remains available during loading.

Build: `node tools/sd-launcher/build-reader.cjs`.
Local evidence: `node tools/sd-launcher/reader-local.cjs`.
Regression suite: `IZ_SD_HOST_GPU=1 node tools/sd-launcher/check.cjs`.
The evidence tool executes the original shaders on a local Vulkan GPU and
the controller from an RX mapping on the host ISA. AArch64 is compiled and
PIC checked. This does not establish Switch hardware qualification.

Do not refresh profile hashes merely to accept an updated module: qualify
its control offsets, GPU header, font metrics and decoder first.
