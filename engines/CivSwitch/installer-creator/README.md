# installer-creator

One script installs the boot-persistent CIV7 savefile-XSS trigger on a console, template included from the PC (the console needs NO existing save).

## Usage

The console must be running the Remote JS Loader (Pico bootstrap or a previously installed trigger).

```
npm install                       # node + terser
python install.py <console-ip> [port] [payload.js]
```

- `<console-ip>`  required
- `[port]`        optional, default 9026 (the Remote JS Loader port)
- `[payload.js]`  optional, default `../core.js` (the shared payload at the repo root) - the shipped default is the Remote JS Loader: after reboot, Load Game brings the loader up at boot.

Everything else is automatic: the payload is wrapped and compressed (terser -> deflate -> inflate-stage, with an offline round-trip check), the save template is uploaded to the loader in chunked connections (~4, a few seconds), then an apply payload locates the `currentTurnDate` record in the template by field hash, splices the compressed stage into it, wipes every file in `save:/saves/Single/`, writes `save:/saves/Single/IZ2S.Civ7Save`, and commits.
Exit code 0 only on `INSTALL DONE`.

After install: reboot the game, main menu -> **Load Game** -> the list builds -> the stager fires -> hex decode -> eval(stage) -> inflate -> your payload runs, every boot.

## Files

- `install.py`        the one tool (compress + upload + apply)
- `save_template.bin` pristine save clone base
- `compress_stage.js` node compression helper (decoder inlined verbatim from PicoSwitch's compress.js; verifies the stage inflates EXACT)
