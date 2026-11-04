# Book Reader

A small text-first ebook reader for **IZ2S Platform** and **IZlauncher**. Read English TXT and DRM-free EPUB books on your Switch, using touch or a controller paired with the console.


## Set up before launching

In the IZ2S App library, open **Book Reader → Reader settings**. This page works before a console is connected or the reader is activated. Add books, choose an appearance and save it. Then select/load the module and open Book Reader in IZlauncher.

The settings page requires the platform's module-settings API. It does not start a console session, activate the reader or launch another application. Other modules can provide the same entry point through their descriptors.

To install this complete module into another compatible platform folder, run `./install.sh --platform /path/to/iz2s-platform`. For an update, stop its session first and add `--replace`; existing books and preferences are preserved and the previous module is backed up.

## Appearance

- Text sizes from 24 to 64 pixels at the native 1080p canvas scale.
- Real regular and bold fonts, with serif and sans-serif choices.
- Three line-spacing and margin presets.
- Landscape, 90° clockwise and 90° counterclockwise rotation. Touch follows the rotation.
- Paper, sepia and night page colors.
- Saved appearance, selected book and reading position between sessions.

## Add books

Use **Add books** or drag English TXT/EPUB files onto the PC settings page. For manual import, copy files into `modules/BookReader/data/books/inbox/`, then choose **Import this folder**.

TXT accepts UTF-8 and Windows-1252, up to 8 MiB. EPUB is text-only, unencrypted and up to 16 MiB as uploaded, with a bounded 16 MiB expanded import. Chapters follow the EPUB spine. Pictures, scripts, DRM, PDF and scanned pages are not implemented. English punctuation is normalized to the supported character set.

Books stay together inside the module:

```text
modules/BookReader/
├── book-reader.izm          # Distributable source/console module
├── server/                 # Import, compression, settings and console bridge
├── switch/                 # Native text renderer, decoder and fonts
└── data/                   # Private; excluded from packages and updates
    ├── settings.json       # Appearance and selected book
    ├── progress.json       # Reading positions
    └── books/
        ├── inbox/          # Original TXT/EPUB files
        ├── compiled/       # Compact .leaf book containers
        └── library.json    # Book list
```

The library holds up to 128 books. The current edition of a book must fit within the reader's 6 MiB data budget, including its font atlas and indexes. Large books may need splitting. The reader checks the layout size before changing an open book.

## Read on Switch

- **Up/Down → A:** choose a book in the library.
- **A, Right or R:** next page.
- **Left or L:** previous page.
- **B:** return to the book library; B in the library returns to IZlauncher.
- **Minus** or the visible **Menu** button: reading menu.
- **Touch:** tap the left/right side to turn a page; tap the middle to open the menu.
- In the menu, use Up/Down to choose an item, Left/Right or A to adjust it, and B to return. A visible **Return to launcher** item is also provided.

## Text storage and decoding

The PC stores each normalized book in a **Brotli-compressed `.leaf` container**, including its original length and SHA-256 checksum. The settings page shows actual before/after sizes; compression depends on the book.

When a book is opened or its typography changes, the PC builds a complete indexed text edition for the console. Common words become single-byte dictionary tokens. The **compute shader decodes the selected page on the Switch GPU**, builds glyph positions and draws it with a local antialiased font atlas. Other pages remain compressed in the console buffer. Ordinary page turns do not fetch rendered images or stream video.

Changing font size or orientation reflows the text and keeps an approximate source-text position. The PC prepares and uploads book editions, processes appearance and library requests, and saves reading progress. Page decoding and drawing run on the console GPU. Operation after PC shutdown has not been validated; this package loads through an established IZ2S console session.

The reader does not need a new CPU executable, interpreter or native decompression adapter. Its bounded dictionary decoder is included in the module's compute shader. See [FORMAT.md](FORMAT.md).

## Build and check

The runtime uses Node.js built-ins and Python 3 with its standard library. A developer build additionally needs Pillow, DejaVu fonts, glslangValidator and spirv-val, available through the platform's normal development prerequisites.

```sh
IZ2S_ROOT=/path/to/iz2s-platform node tools/build.cjs
node ../../distribution/verify.cjs
node tools/package.cjs --platform /path/to/iz2s-platform
```

`tools/check-gpu.cjs` additionally compares actual desktop Vulkan shader decoding with the original page text and renders landscape/portrait previews. It needs an IZ2S development checkout and a desktop Vulkan driver. Desktop checks are not a physical Switch qualification.

## License

Original module code is GPL-2.0-or-later. DejaVu font terms are retained in `licenses/DejaVu.txt`. Platform SDK headers retain their existing platform terms; see `THIRD_PARTY.json`.

## Platform status and SD convention

This bundled module currently declares Linux x64/ARM64 hosts. Windows/macOS platform installers do not qualify this module for those hosts; OS/native-session testing is pending. See the platform COMPATIBILITY.md. Its future console SD data location is `SD:/IZ2S/Homebrew/Modules/<module.id>/`; adoption is explicit, and existing data is not migrated automatically. PC-local module data remains private.
