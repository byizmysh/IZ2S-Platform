Persistent installer from [Gezine/CivSwitch](https://github.com/Gezine/CivSwitch/blob/3238a3e3340fd19a9dbd5cde56f3b50f9bc5d97f/installer-creator), commit `3238a3e3340fd19a9dbd5cde56f3b50f9bc5d97f`.

`install.py` only adapts the default payload path to this repository. `compress_stage.js` and [save_template.bin](https://github.com/Gezine/CivSwitch/blob/3238a3e3340fd19a9dbd5cde56f3b50f9bc5d97f/installer-creator/save_template.bin) are included from the same version. Gezine's original MIT license is preserved in [LICENSES/Gezine-MIT.txt](../LICENSES/Gezine-MIT.txt).

The loader accepts about 500 KiB per connection. `install.py` encodes the save template as Base64 and uploads it in four separate chunks of at most 400,000 characters. Each chunk is kept in `window.__tpl` on the console. A final JS payload reassembles the template and writes the save.

The installer replaces the game's single-player saves. Run it through `npm start`, or use `ldn/.venv/bin/python installer/install.py <console-ip>` once the Remote JS Loader is ready.
