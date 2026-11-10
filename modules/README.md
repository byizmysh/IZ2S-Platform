# Applications bundled with 0.22

| App | Packaging |
| --- | --- |
| [File Exchange](iz-exchange/README.md) | `iz-exchange` IZM; title-owned files and FTP |
| [File Exchange FS](iz-exchange-fs/README.md) | `iz-exchange-fs` IZM; retained FS exports, depends on File Exchange |
| [Book Reader](BookReader/README.md) | `book-reader` IZM in `BookReader`; TXT/EPUB library |
| [IZPSX emulator](izpsx/README.md) | `izpsx` IZM, version 0.5.0; console-local PlayStation emulation |
| [Super File Bros.](../tools/fs-rpc-browser/README.md) | Integrated PC browser/file service, not another console IZM |

The panel packs the four console modules from the included source/assets at fresh startup. Their host declarations and native/FS requirements remain enforced. Personal books, games, BIOS, saves, pairing credentials, previous versions and per-module runtime data are excluded.

BlitBun, VacooTube, IZDOOM, IZPS2, controller labs and test packages are not bundled. Users can obtain independent compatible modules separately. SD app storage uses `SD:/IZ2S/Homebrew/Modules/<module.id>/`; RAM apps require explicit selection and are not automatically restored across sessions. See [SDK storage](../sdk/SD-STORAGE.md).
