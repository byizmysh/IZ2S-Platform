# IZ2S 0.3.5

IZ2S 0.3.5 updates the PC panel and console launcher together. It improves menu startup, makes graphics resource cleanup more robust, adds on-demand shared libraries for compatible NRO applications, and fixes the PC connection check that could stop with `ModuleNotFoundError: No module named 'send_js'`.

The comparison is against the final updated 0.3.4 Linux archive. The Split Screen HOME/close fixes already delivered in the silent 0.3.4 updates are retained, not presented as new features.

## PC panel

- Fixed the Linux connection check after restoring PC networking. It no longer imports `send_js` from a removed component or starts a Python subprocess just to check the console connection.
- The check now uses the same bounded TCP protocol probe as device discovery. It requires the expected unique response, rather than treating an open port as a successful connection.
- Cancelling a connection check closes its socket. A timeout or an unexpected response invalidates an earlier ready state instead of leaving the panel ready to continue with an unverified connection.
- Added the network session and shared discovery entry points to release integrity checks. These files are used by child processes and were not covered by ordinary descriptor discovery alone.
- Added regression coverage for checks with and without prepared Python dependencies, an incorrect response after a previous successful check, and cancellation of a silent peer. This coverage is included in CI.
- Updated the panel, browser title, shortcut, package metadata, SD archive metadata and four UI dictionaries to 0.3.5. The browser title keeps the current version when switching sections. The new installation uses local port 39535 and a version-specific desktop shortcut.
- Updated the README, installation guide, compatibility notes and FAQ for the complete package and the new runtime. User-facing release documents no longer use internal development build names.

## Menu startup and graphics cache

- Split catalog discovery from tile artwork preparation. The first pass validates application metadata and prepares the first tile. Other NRO icons and ordinary IZA artwork can load after the first displayed frame.
- Deferred artwork runs one item at a time while the menu has no input to handle. The selected pending tile has priority. No additional worker thread or permanent background scanner is created.
- Applications remain selectable while their artwork is loading. Existing managed-application validation and launch-time checks remain in place.
- A catalog change cancels old artwork tasks. Before using a deferred IZA tile, the launcher checks its header again and verifies the tile CRC. Invalid artwork keeps its placeholder rather than publishing unchecked pixels.
- Added a separate `catalog_artwork_ready` timing mark alongside `first_present`, so menu visibility and completion of decorative loading can be measured separately.
- Added a Vulkan cache format keyed by the actual shader sections in their roles, rather than every byte of the IZA container. Changes to unrelated menu data do not invalidate that new cache.
- Existing cache files are accepted only when their original exact-image key matches. Migration is saved during idle time; the old cache stays available for rollback.
- GPU identity, driver identity, cache headers, size bounds and payload SHA-256 remain checked. Shader or driver changes still require compilation. Cache handling does not relax SD package verification.
- The visible launcher version is now **0.3.5**, with a wider, centered version badge. This shader change can trigger a one-time menu compilation on the first launch of 0.3.5.

## Graphics resource cleanup

The embedded IZA Vulkan driver includes six targeted OunceVK fixes, while retaining the established driver and launcher startup path.

- GPU memory whose unmap or release failed is kept out of the reuse pool.
- Slow memory requests are allowed to complete without blindly submitting the same operation again.
- Ownership records, sparse views and virtual address reservations survive failed teardown, allowing a later valid cleanup instead of premature reuse.
- A partially released sparse mapping keeps its remaining chunks until the surviving ranges have been remapped.
- Failed sparse views are retired as a whole instead of reconstructing fragments of an invalid view.
- Swapchain cleanup releases image acquisitions that were never presented.

These changes address resource-lifetime hazards. They are not a claim that every cause of error 2162-0002 or every Load Game failure has been eliminated.

## NRO runtime and shared libraries

- Added an expandable RO image list and explicit release of the images belonging to a completed NRO session.
- Image release follows thread and JIT cleanup. If release cannot be confirmed, the loader does not reuse the affected staging memory.
- Added dependency loading through `DT_NEEDED` and runtime dynamic linking support. Libraries are searched for in the application's directory, then in `SD:/iz2s/lib`.
- Included eight matching shared libraries from the reviewed KaiHEN release: `libounce`, `libstdc++`, `libouncevk`, `libouncevk-gl`, `libvulkan`, `libEGL`, `libGL` and `libGLESv2`.
- Compatible dynamic NRO applications can use the libraries' C++ runtime, Vulkan/GL/EGL/GLES and AudioIn interfaces. Libraries load when requested; the menu does not load all eight at startup.
- Ordinary NRO load failures can return to the menu after successful cleanup. Failed cleanup stops reuse and leaves a diagnostic record.
- Added readable menu messages for NRO ABI errors 2346-0200 through 2346-0206 and cleanup diagnostics in `/iz2s/log/hen/nro-cleanup.txt`.
- NRO applications still run full screen. This release does not restore NRO Split or add the new shared-library interfaces to the IZA ABI. IZA Split retains its established embedded runtime.

## Package and upgrade

- Both the panel and launcher use version **0.3.5**. `IZ2S-0.3.5-linux.zip` contains the Linux installer and the matching `iz2sciv-core-sd.zip` next to `install.sh`.
- The SD kit still includes IZ2S Cheese 0.8.7, IZPSX 0.5.3, Foldrift 0.3.3 and NX2 BreRUN 0.6.0. These application binaries are unchanged. IZPS2 remains a separate library download; its local export retains `vu-split-persist-off.flag`.
- Runtime libraries, licenses, native build inputs and outputs, package inventories and SD hashes are shipped as a consistent set. No console-specific GPU cache, keys, BIOS, games or user state is included.
- Compared with the published 0.3.4 package, the RO component has changed. Use the complete SD package and a launch save compatible with that runtime. Updating only the menu is insufficient for that migration.
- The menu-only update from the most recent compatible local runtime retains its boot compatibility. Do not infer compatibility from the displayed version number alone.
- Linux x64 and ARM64 remain the package targets. Windows installers, the old standalone CivSwitch engines and VacooTube remain excluded.

## Retained fixes and verification scope

The silent 0.3.4 fixes that pause Split rendering in HOME and finish GPU work before closing are included. The earlier idle-rendering reduction, deferred incoming-app import, catalog reuse, faster FTP path, PC Link setup UI, offline library browsing, compact mobile filters and four-language FAQ are also retained.

The user confirmed that the latest menu optimization loaded quickly without errors on Switch. Earlier user checks also confirmed Split IZPSX + IZtwiNS2 followed by HOME and X with the cleanup fixes. The final 0.3.5 versioned binaries are rebuilt artifacts and still need their own console run. Dynamic-library APIs, Linux ARM64 execution and full end-to-end installation require separate hardware checks; no new measured speed or FPS improvement is claimed here.
