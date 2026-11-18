# Foldrift

Autonomous file manager for IZ2S. Its directory and context-menu workflow draws on [OunceShell](https://github.com/kurumuz/ounceshell). The shaders, controller and artwork are Foldrift source.

The PC installation is in `modules/Foldrift/`. Install `foldrift.iza` through Add App. To copy it to the SD card manually, save it as `SD:/iz2s/modules/foldrift/launcher.iza`. The signed application ID remains unchanged for update verification. IZ2S uses the Foldrift folder name independently of that ID.

On builds of IZ2S with full SD support for Foldrift, the app opens at `SD:/`. Other builds open the app folder. The heading shows which folder is available.

- D-pad Up and Down select a row. L and R turn pages.
- A opens folders or shows a text or hex file preview. B goes to the parent.
- X opens the context menu. Copy or cut a file, open the destination folder, and press Y to paste it. A file with the same destination name is kept in place.
- Open the context menu with X to create a folder, create an empty file or rename a file. Use D-pad, A, X, Y and Plus to select letters, type, erase, shift case and save. B cancels.
- Deleting a file requires confirmation. Files can be moved or renamed. Folders can be created and browsed using the runtime's supported storage operations.
- Y refreshes the list when the clipboard is empty. Clear the clipboard from the context menu. In the file list, release Plus to return to IZlauncher.

The file list and preview render basic Latin and Cyrillic. Other scripts use a placeholder. The keyboard enters Latin letters, digits and common punctuation. Existing file names and data retain their bytes. The font atlas uses the platform’s DejaVu Sans Mono font. See `../../language/fonts/LICENSE.txt`.

Build with Python and Pillow, glslangValidator, spirv-val, Node.js and an AArch64 GNU toolchain. Set `IZ_SB4_INCLUDE` to the directory containing the SDK's `sb4/` headers and set `IZ_AARCH64_PREFIX` if the compiler is outside PATH. Run `./build.sh` from any working directory. The console image is written to `dist/foldrift/` in the platform root.
