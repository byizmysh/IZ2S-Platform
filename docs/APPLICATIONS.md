# Application storage and updates

The SD kit includes IZ2S Cheese 0.8.7, IZPSX 0.5.3, IZPS2 0.5.0 and Foldrift 0.3.3. The PC library also offers Book Reader 0.1.0, BlitBun 1.15.8 and VacooTube 1.5.7 separately.

Install through App library or its SD export action. A downloaded package can contain executable companions: do not rename the whole package to `launcher.iza`. Keep manifests, signatures, `.izdata` resources and licenses. The usual entry is `SD:/iz2s/modules/<id>/launcher.iza`; Foldrift uses the folder `foldrift`.

BlitBun and VacooTube PC media hosts require Linux. Native capture uses FFmpeg, X11/XWayland and PulseAudio/PipeWire; browser sharing requires the browser's explicit selection. The Linux installation archive includes clean Electron runtimes without a user profile or login. Book Reader imports TXT/EPUB through its panel settings. IZ2S Cheese uses its own catalog/download interface.

Before updating, back up replaced files and preserve application settings, BIOS, games and saves. Neither BIOS nor game images are supplied.

## IZPSX BIOS and games

BIOS and game images are not included. Add your own **512 KiB (524,288-byte)** BIOS at:

```text
SD:/iz2s/modules/izpsx/bios/bios.bin
```

Put games in:

```text
SD:/iz2s/modules/izpsx/games/<game name>/
```

Keep each CUE file with all referenced BIN/IMG tracks; put PBP files in the same per-game layout.

When updating, preserve settings and these folders:

```text
bios/
games/
saves/
```

IZPSX 0.5.3 uses these paths.

## IZPS2 BIOS and games

BIOS and game images are not included. Put your own **4 MiB** PS2 BIOS dump in `SD:/iz2s/modules/izps2/bios/` and ISO/CHD game images in `SD:/iz2s/modules/izps2/games/`. Create these folders if needed. Preserve BIOS, games, saves and settings when updating.
