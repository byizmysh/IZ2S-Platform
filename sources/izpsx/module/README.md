# IZPSX emulator 0.5.3

Install through IZ2S Platform App library or copy the complete SD export. Keep `launcher.iza`, `sd-bundle.json` and its companion assets together in `SD:/iz2s/modules/izpsx/`.

## Add your BIOS and games

BIOS and game images are **not included**. Add your own files after installing the emulator. These are paths on the Switch SD card:

| File | Exact destination |
| --- | --- |
| PlayStation BIOS, exactly **512 KiB (524,288 bytes)** | `SD:/iz2s/modules/izpsx/bios/bios.bin` |
| CUE/BIN game | `SD:/iz2s/modules/izpsx/games/<game name>/disc.cue` and its referenced BIN tracks in the same folder |
| PBP game | `SD:/iz2s/modules/izpsx/games/<game name>/game.pbp` |

Create `bios/` and `games/` if they do not exist. The BIOS filename must be `bios.bin`; keep game filenames and CUE track references consistent. Extract compressed game downloads before copying them. Do not put BIOS or games beside `launcher.iza`. Add them through **Games and BIOS** in the PC panel, FTP, or a mounted SD card. Preserve `bios/`, `games/`, `saves/` and settings when updating the application.


Memory cards are saved under `SD:/iz2s/modules/izpsx/saves/`. Plus is PS1 Start; Minus opens the emulator menu. Choose Exit there to return to IZ2S.

Original licenses remain in LICENSE and FONT-LICENSE.txt.
