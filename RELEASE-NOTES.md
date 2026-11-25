# IZ2S for Civilization VII - 0.3.2 Stability-1

**Linux-only local candidate, 25 November 2026. Not yet published to GitHub.**

PC panel **0.3.2** and console launcher **0.32** are different version series. This package uses native revision **RUN32-0.32-Civ7-Lifecycle-Audit-1**.

## Panel

- Civilization VII is the only entry point. Old SmileBASIC 4 selection and device preparation routes are removed from the shipped setup.
- Four-step setup: SD card, PC preparation, connection with automatic save installation, and launch. A successful reconnect proceeds to save installation without a second manual action.
- Launch view prioritizes an active PC Link / FTP connection; launch help is collapsible and repeating setup starts the full procedure.
- Console IP selection sits directly below session status. Conditional migration instructions and important notes are highlighted without hiding details.
- Revised sidebar branding identifies IZ2S for Civilization VII. Author identity is IZMYSH. English, Russian, Chinese and Spanish panel text includes the updated requirements and ten-question FAQ.
- Requirements explicitly name Switch 2 firmware 23.0.1 and the Nintendo Switch 1 cartridge of Civilization VII 1.0.0. Game and save-deletion menu paths stay in English.

## Launcher and SD

- Revised application title/version presentation, aligned controls and battery/time display. X opens Add app; A opens the selected application. The Add app tile is removed.
- Permanent shader-cache replacement fix: failed replacement no longer silently leaves an obsolete cache in place. Writes are checked and recovery of a valid temporary cache is supported; driver and shader validation remain enabled.
- In the diagnosed warm-cache session, native first presentation fell from about 14.41 seconds to 0.203 seconds after restoring the valid cache. This measures the native menu stage, not the complete Civilization VII / R launch. It is not a timing guarantee for every console or a validation of this newly packaged installer.
- The SD kit contains IZ2S Cheese 0.8.7, IZPSX 0.5.3, IZPS2 0.5.0 and Foldrift 0.3.3, with companion assets and licenses. Other applications remain available separately in the PC library.
- Runtime, SD manifest and launch-save compatibility checks are retained. Console-generated caches and user content are excluded from release packages.

## Packaging maintenance

- The clean installer inventory includes the Civ7 profile entry, which was previously present only in the working directory.
- Windows installation files and host binaries are excluded from this Linux-only release.
- The old developer-only wireless CLI wrapper is replaced by guidance to the packaged panel; personal paths and an unshipped local environment are no longer required by a public instruction.
- Regression checks now cover the current Civ7-only navigation and automatic save flow.

## Updating and validation

Use the matching SD kit and follow the [full procedure](docs/INSTALLATION.md) when updating the native runtime. A PC-only documentation or layout change does not require changing a console save.

Release preparation verifies source/runtime inventories, native build bindings, application exports and archive readback. Successful packaging is separate from a successful console boot. On-console validation of the newly packaged build and native Linux ARM64 host installation must be recorded separately. Full native corresponding-source limitations remain documented in [SOURCES.md](SOURCES.md).

## Stability-1

- Uses the same native menu and IZPS2 engine bytes as the successful console test; native diagnostic instrumentation is retained.
- Disables optional IZPS2 VU disk persistence by default. The full SD ZIP and individual IZPS2 download archives include `vu-split-persist-off.flag`. Extract the full SD ZIP to install it; do not rely on Add app / PC Link to install this auxiliary file. The current native importer preserves an existing flag during an application update but does not extract it from an app archive. If the IZPS2 folder is deleted, restore the flag from this SD kit before launching IZPS2.
- Ordinary VU JIT execution remains enabled. Subsequent sessions may need to compile code again; no controlled FPS comparison is claimed. Game saves and the menu startup cache are unchanged.
- Preserves integrity checks, asset bindings, licenses and the four-app SD layout.
- Carries forward the Civ7 save installer change that reads back and verifies the staged and committed save.

Hardware evidence: one IZPS2 exit returned to the menu with all 52 files closed and no outstanding tracked worker/image state. The user additionally repeated IZtwiNS2 launch/exit and Civilization VII HOME -> X closure without error 2162-0002. The exact shared-process fault mechanism remains undetermined. This package has been reconstructed from those components; its complete installation/Load Game/R cycle still requires separate hardware qualification.
