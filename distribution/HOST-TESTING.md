# Host acceptance for 0.22

This candidate targets Linux x64/ARM64 only. These are acceptance steps, not a record that the steps have passed. Do not publish a release or run remote workflows solely to fill this file.

1. Extract into a writable path containing spaces and a non-ASCII user/path component. Install through the OS entry point, retaining its whole source tree.
2. Check dependency download failures, checksums, no-shortcut/no-launch behavior and reuse of an existing application.
3. Open offline Preview and check all four panel languages and responsive layouts.
4. Only on a supported declared host, use the correct fresh game screen and qualify input, launcher, FS acquisition, module lifecycle and Stop. Do not bypass native host restrictions to complete the test.
5. Test Civ7 First Setup separately from later LAN/Wi-Fi Launch; protect the user's keys and game-save backup.
6. Test the four bundled console modules and integrated Super File Bros.; verify RAM/SD badges and explicit startup selection.
7. Test shared file-service views, cancellation, same-file resume, lost connection and normal/uncertain cleanup.
8. Check sleep/recovery, closing/reopening browser views and shutting down the PC panel. Record unresolved retained resources accurately.

Record OS/runtime versions, commit/source-manifest hash, app versions, exact steps and physical-console evidence. Local Linux unit tests or archive generation do not substitute for ARM64, long-session or every-game qualification.
