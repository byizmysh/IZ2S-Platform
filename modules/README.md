# Bundled IZ2S modules

- **[File Exchange](iz-exchange/README.md)** (`iz-exchange`): SmileBASIC title folders and FTP.
- **[File Exchange FS](iz-exchange-fs/README.md)** (`iz-exchange-fs`): installed-game exports through a retained FS RPC endpoint; requires File Exchange.
- **[Book Reader](BookReader/README.md)** (`book-reader`, directory `BookReader`): English TXT/EPUB reading and prelaunch library settings.

Source and compiled console assets are included. The panel builds the portable IZM archives before a fresh start. Other modules can be installed here separately. Per-module `data/`, books, reading positions, installed runtimes and previous versions stay private and are excluded from the release.

The common console SD directory is `SD:/IZ2S/Homebrew/Modules/<module.id>/`, independent of this PC installation directory. See [SDK storage guidance](../sdk/SD-STORAGE.md). Existing module data is not moved automatically.

## Additional modules

- **[VacooTube](https://github.com/byizmysh/VacooTube):** use its own installation instructions and release packages.
- **[BlitBun](https://github.com/byizmysh/BlitBun):** use its own installation instructions and release packages.
- **Emulators and other modules:** obtain a compatible package from its developer and follow its installation guide. They are not included in the platform archive. Check the module's host OS, runtime and console requirements before installation.

[Super File Bros.](https://github.com/byizmysh/super-file-bros) is integrated into IZ2S and also distributed as an independent standalone project.
