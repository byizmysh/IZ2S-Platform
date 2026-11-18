# VacooTube host runtime

This is the PC half of the module, not a separate console app. Use the root [installation guide](../../README.md) after installing IZ2S Platform.

`install.cjs` prepares the included app source on official Electron; `fork-source.cjs` installs locked production npm dependencies into runtime/source/<digest>/; `bootstrap.cjs` creates the scoped offscreen capture/input path; `audio.cjs` owns the silent output sink. Mutable binaries and profiles remain in the module's runtime/ and data/.

Run `node modules/VacooTube/server/local-package/install.cjs` from the installed platform, or use the VacooTube card's explicit runtime-install action. `--check` only verifies/resolves the current runtime. `--prepare-source` refreshes the included source dependencies. No command uploads a bootstrap, signs in or clears remote history.
