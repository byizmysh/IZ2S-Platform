# libsb4 native services v1

Provider sources for libounce `9382b1ec48ac`. Copy `native-services.h` to `include/sb4/` and the three other files to `source/` in libsb4, then rebuild `libsb4.a`. The provider extends the existing `Sb4Compat.resolve_c`; ABI 7 remains unchanged. Include both `.def` files in the Makefile dependencies for `compat.o`.

`sb4NativeServicesVersion` returns 1. `native-services.def` lists network, TLS, software keyboard, input, hashing and thread imports with their original libounce prototypes. Clients must pin matching headers, resolve all imports before use, balance SSL/NIFM references, borrow already-initialized sockets without closing them, and join every module worker before image unload.

`sb4GetApplicationIoCompat` is a launcher-only policy hook. It adds process stdio functions for an already-authorized installer. It is not exported through `resolve_c`, and ordinary modules retain the existing scoped storage API. The launcher keeps its prior Store installer grant. It also links the legacy
Store handler through `switch/iza/host.c` for older packages. The native services
provider remains usable by independent Store images.

Store build and ARM64 lifecycle checks are documented in `modules/iz2s-cheese/docs/building.md`.
