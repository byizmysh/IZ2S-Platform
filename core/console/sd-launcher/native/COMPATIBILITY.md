# Native module compatibility

IZlauncher and IZPSlauncher share the same SD supervisor. A module declares
its minimum native ABI through `switch/app.json`; it does not pin a launcher
release number or an internal supervisor structure.

`native-app.h` is the public contract. Each ABI revision preserves the earlier
prefixes. Consumers validate the required prefix size, environment version and
required callbacks before use. Suffix fields are optional. A provider that
cannot supply the declared ABI rejects the package before entering its code.

`app->owner` is opaque. It may be returned unchanged to public callbacks; a
module must never cast it to `IzNative`, `IzFs` or `IzContext`. Those structures
are private and can change as the launcher develops. App ABI 5 exposes the
borrowed SDK/process/filesystem environment needed by IZPSX. SDK entry bytes
still require validation; ABI compatibility does not identify an SDK build.

`IZ_NATIVE_SOURCE_RAM` is an optional source flag and preserves the ABI layout.
Its absence on an older provider is not a reason to refuse execution.

Known INIT/FRAME failures must propagate their code to the supervisor so it can
retain the launch failure while returning through STOP. An uncertain resource
outcome still retains the owner and cannot be converted to a clean exit.
