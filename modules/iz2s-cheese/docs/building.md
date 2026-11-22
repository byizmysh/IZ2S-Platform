# Build IZ2S Cheese

Set `IZ_DEVKITPRO` (or `DEVKITPRO`) to ounce-devkit 20261114 with libounce `9382b1ec48ac`. Install Node.js, Python, Pillow, `glslangValidator` and `spirv-val`. Set `IZ_STORE_FONT_DIR` to a directory containing DejaVu Sans regular and bold fonts.

```sh
sh build.sh nro
IZ2S_PLATFORM_ROOT=/path/to/IZ2S-Platform sh build.sh both
```

For IZA, install the updated libsb4 headers alongside the devkitPro directory. Set `IZ_RELOCATABLE_PACKER` to the SDK `relocatable.py` if it is not in the default sibling RUN23 tree. `IZ_STORE_OUT` overrides the release directory. `IZ_STORE_CHECK=1` enables package and catalog tests; these also require a host C compiler and OpenSSL development headers.

## Native module

`switch/iza/entry.c` loads the module-owned `asset-<sha256>.izdata` using the SDK image loader. The resource contains `main.c`, `store.c`, `ui.c`, `download.c` and the icon decoder. Their writable state is allocated with the image. IZ2S Cheese executes its own application code and uses the launcher SDK for system services. The launcher must grant application I/O to the `iz2s-cheese` module ID.

The image resolves the explicit imports in `switch/iza/imports.json` before running application code. Native services v1 uses the pinned libounce C ABI. Missing services fail before a worker starts, with `startup-error.txt` or `missing-sdk-import.txt` in the module folder. TLS certificate and hostname verification remain enabled. Shared launcher sockets are borrowed and never closed by IZ2S Cheese.

The existing launcher policy grants this installer process-level SD I/O. This does not grant general modules whole-SD access or change the separate scoped storage API. SDK network, TLS, keyboard, input and thread functions are reusable by other applications; they contain no application logic.

STOP cancels an active download and returns `STOP_PENDING` while the worker is alive. The module joins and closes the worker before unloading its image. Image resources and the previous executable are retained when updating the running IZA.

## Checks

```sh
python3 check.py /path/to/release 1 1
python3 tests/self-update.py /path/to/release
```

For the ARM64 lifecycle test, set `IZ_LINUX_SYSROOT`, `IZ_SB4_INCLUDE`, and `IZ_QEMU_AARCH64`, then run `sh tests/iza-lifecycle.sh /path/to/release`. It executes the actual module with simulated device services: relocation, missing SDK/imports, repeated startup, keyboard calls, DNS failure, TLS/socket cleanup and join-before-unload. Physical network and keyboard behavior still requires a console check.

`python3 tests/catalog-size.py` exercises dynamic catalogs and both thumbnail formats with ASan/UBSan. On hosts where LeakSanitizer is unavailable under process tracing, use `ASAN_OPTIONS=detect_leaks=0`.
