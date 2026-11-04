# Module files on the console SD card

Every module has a permanent directory:

```text
SD:/IZ2S/Homebrew/Modules/<module.id>/
```

Use the lowercase `id` from `module.json`, not the display name, version or PC installation directory. For example, `id: "ps2-emulator"` uses `SD:/IZ2S/Homebrew/Modules/ps2-emulator/`. `book-reader` uses `book-reader/` even though its PC directory is `BookReader/`. Keep the ID unchanged across releases so existing module files remain accessible.

IZ2S creates the base and directories of installed modules when **SD & console storage** opens through FS RPC. This does not depend on choosing FTP or an external file manager. No SD access occurs during package installation, building or offline panel use. Reopen the storage export after installing another module, or let that module call `ensure` itself.

## SDK context

Module lifecycle functions, module settings, session hooks and console host-runtime components receive `context.sdStorage`. The same object is available as `context.services.sdStorage` in lifecycle/settings contexts. Code importing the SDK directly can use `sdk.sdStorage(moduleId)`.

```js
const location = context.sdStorage;
location.path;        // /IZ2S/Homebrew/Modules/ps2-emulator
location.virtualPath; // /sd/IZ2S/Homebrew/Modules/ps2-emulator
location.displayPath; // SD:/IZ2S/Homebrew/Modules/ps2-emulator/

// With an already-open writable console SD adapter:
await location.ensure(storage);
```

`storage` must provide `writableMounts: ['sd']`, async `list(virtualPath)` returning `{name, type: 'dir' | 'file'}` entries, and async `mkdir(virtualPath)`. If it provides `commit()`, IZ2S calls it after creating directories. The existing FS RPC storage adapter implements this interface. `ensure` returns the paths above, creates missing parents and preserves existing contents. Calls through this helper are serialized per adapter. A file blocking a required directory or a failed write is reported to the caller; it is never replaced or silently retried.

`ensure` does not acquire native memory, start an FTP server or open another filesystem session. Modules owning a native SD mount can use `location.path` relative to that filesystem root, and perform equivalent directory creation through their own filesystem API. Bindings such as `sdmc:` may prepend their mount name. These are console paths: do not pass them to the PC's `node:fs` or OS path resolver.

`context.dataDirectory` continues to hold PC-local configuration/cache. Reading the SD path needs no connected console. Preparing the directory needs a writable SD adapter; absence of one gives a clear error without disturbing the console.

## Module migration

Module authors choose their files and subdirectories, for example:

```text
SD:/IZ2S/Homebrew/Modules/ps2-emulator/
  bios/
  games/
  saves/
  config/
```

These PS2 subdirectories are examples, not required or automatically created. Existing modules must explicitly adopt the path for reads and writes. IZ2S does not move old data, rewrite module-specific paths or delete SD contents on module removal. SD files therefore survive module updates and changes to the PC/server installation.
