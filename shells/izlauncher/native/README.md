# Foldrift storage in IZ2S

Apply `sd-storage.patch` to the native IZ2S sources. Compile the patched loader with the libsb4 SDK, `sdk/sb4-storage-scope.h`, the headers in this directory, and the vendored Monocypher include directory. Link `monocypher.c` and `monocypher-ed25519.c` with the launcher. The provider uses the mounted SD filesystem.

`iz-file-deck-publisher.h` contains the release publisher's Ed25519 public key and the detached signature for the bundled Foldrift executable. This lets the regular App Library installation open `SD:/` without copying a separate file. Store the signing key outside the launcher and platform distribution.

To sign a new Foldrift executable on the release host, run `node sign-deck.cjs sign Foldrift.iza /secure/publisher.pem launcher.iza.sig`. Run the corresponding `verify` command with the public PEM before packaging. Place the 64-byte detached signature next to the updated executable at `SD:/iz2s/modules/foldrift/launcher.iza.sig`, or embed it as the bundled signature in the next IZ2S release. The signature binds the Foldrift module ID, the whole-SD capability and the SHA-256 of the exact executable. If neither signature verifies, IZ2S supplies the module directory.

After updating the native runtime, close the game hosting IZ2S and reopen it while holding R. Open Foldrift and verify that it shows `SD:/` with Wi-Fi disabled.

The Monocypher files are taken from [Monocypher 4.0.3](https://monocypher.org/download/) under the accompanying license. The runtime uses its standard Ed25519 verifier.
