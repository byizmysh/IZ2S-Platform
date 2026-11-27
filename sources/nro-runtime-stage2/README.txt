IZ2S 0.34 - NRO runtime integration, local candidate

Scope
- RO uses a growing NRO image list and ReleaseNros. Failed unmaps remain recorded.
- The outer loader releases all images only after a whole NRO returns, after its
  thread/JIT cleanup. It does not participate in individual IZA Split pane exits.
- DT_NEEDED libraries load from the program directory, then sdmc:/iz2s/lib.
  Optional user preloads use sdmc:/iz2s/preload; no preload directory is bundled.
- The default IZ2S menu remains embedded in game/main. No standalone menu NRO,
  hbmenu or KaiHEN folder is installed.
- Failed loads return to IZ2S only after successful cleanup. If cleanup fails,
  staging memory is not reused and the process ends with an SD diagnostic.
- A new ReleaseNros probe runs before the first menu mapping to reject old RO.
- The menu explains Homebrew ABI results 2346-0200 through 2346-0206. Details
  remain in /iz2s/log/hen/nro-cleanup.txt.

Libraries
The eight files listed in shared-libraries.json are byte-identical to the
KaiHEN 20261127.1 release. libounce 20261127 supplies dynamic linking, C++ runtime
support and AudioIn through aud:u. OunceVK 20261123.1 supplies Vulkan and GL/EGL/
GLES, including its newer GPU memory accounting and compiler changes, for
programs that load those libraries. They are not desktop Linux shared objects.
They are not loaded when the embedded menu or an ordinary static NRO starts.
Existing IZA modules still use the validated static stage 1 Vulkan driver.
This does not add NRO Split or GL/audio imports to the IZA module ABI.

Preserved contracts
- bootstrap.bin, core/main, NPDM, ROMFS, menu idle policy, deferred startup,
  FTP behavior and IZA Split shutdown are unchanged.
- The outer loader stays on the existing static libounce_core and compiler.
  Only the independent dynamic-image parser is added there. The new shared
  libounce is private to the dynamically linked program.
- The newer optional HEN fsp-ldr root override is not enabled: the existing
  bootstrap/HEN FS contract is retained. Apps requiring that newer service
  need a separate HEN integration; this is not a full replacement of KaiHEN.
- Existing vu-split-persist-off.flag is not modified.

Build
python3 sources/nro-runtime-stage2/build.py --sdk /path/to/devkitpro \
  --menu /path/to/embedded-menu-reference.nro --output /path/to/build
Build the menu with sources/run32-launcher/status/build-split.py --menu-only,
using the stage 1 OunceVK archive and pinned app version table. Build the IZA
interface with sources/run32-launcher/build.cjs. build-record.json records exact
input hashes, SDK library hash and commands; menu-build-record.json binds the
menu inputs. The source provenance is in upstream.json.

Validation
RO wire/image/session/mapping and loader cycle tests run with ASan/UBSan.
The fixtures cover 300 image records, 300 app/menu cycles, failed unmaps,
load failures, ABI configuration and library dependency/relocation checks.
The library host test does not execute AArch64 constructors or GPU operations.
No success on actual console hardware is claimed for this stage yet.

Installation
This changes ro/main. Use the complete SD kit together with the panel from this
candidate. Fully restart the console and install the matching launch save
(boot revision iz2s-0.3.4-ro2), then test Load Game and R launch. An old save or
an old RO still resident in memory is incompatible. Updating only game/main
is insufficient. Do not use a UI-only hot update to install this runtime.

Next hardware checks
1. Cold Load Game after matched-save installation, then R launch.
2. An existing static NRO: launch and return repeatedly.
3. A compatible dynamic NRO: launch and return; missing-library error recovery.
4. Split IZPSX + IZtwiNS2, return to HOME, close with X.

Upstream
https://github.com/ouncebrew/kaihen/releases/tag/20261127.1
https://github.com/ouncebrew/ounce-hbloader
https://github.com/ouncebrew/libounce/releases/tag/20261127
https://github.com/ouncebrew/ouncevk/releases/tag/20261123.1
