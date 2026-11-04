# Installation-to-shutdown test plan

This is a manual acceptance plan for the **local 0.21 candidate**. It has not been executed on Windows/macOS. Do not upload logs, run a remote workflow or publish a release just to complete this file.

Record OS/version, CPU architecture, candidate archive SHA-256, browser/version, input device/firmware, console system/SmileBASIC versions, engine, shell and extra modules. Keep access URLs, pairing keys, SSH files, books and filesystem listings private.

## 1. Clean PC installation

- Start with a normal user account and no global Node/Python/Pillow, where possible. Test a folder with spaces and a non-ASCII user name.
- Extract the complete package. Open the OS entry file. Confirm only appropriate file/firewall permissions or Linux package-manager approval are requested.
- Confirm architecture selection, HTTPS downloads, checksum handling, private Python/Pillow, SSH availability and bundled-font checks. No global Node/Python or SSH server should be installed.
- Confirm the printed new installation directory, receipt `data/state/installation.json`, normal shortcut and initial browser request.
- Open the installed Start file again. It should reuse a healthy running panel. Check the browser window and private startup log.
- Verify duplicate destination refusal and failed-download/checksum behavior preserve an existing installation. Test without administrator rights and under the intended network/proxy policy.

Windows/macOS: **these checks are pending on their actual OS.** Linux-only package/build checks are not evidence of success here.

## 2. Panel and preview checks

Use the installed private Node directly if a graphical entry file fails:

```text
Windows: data\runtime\node\node.exe start.cjs --preview --no-open
macOS:   data/runtime/node/bin/node start.cjs --preview --no-open
Linux:   tools/node start.cjs --preview --no-open
```

Use only the private URL printed for that preview. Verify page loading, language selection, App library, installed module metadata and expected compatibility messages. Preview must not discover/type/flash devices. Verify Stop/reopen UI with a suitable mock rather than sending console commands from a preview.

The current native-session descriptors are Linux-only. On Windows/macOS, the correct result is an unsupported-host restriction before a native session starts. Do not remove it merely to mark this checklist passed. Qualification requires the port/review work below first.

## 3. Native-host port gate

Before enabling a new host, review all relevant engine/shell/input/module platform declarations together. Verify directory linking/materialization (Windows junctions versus privileged symlinks), retained FS named pipes/Unix sockets and their path validation, Python executables, process-tree ownership and graceful stop/restart. Confirm uncertain native cleanup preserves the restart requirement. All modules with native executables need host-specific programs or an explicitly unavailable state.

Only after that review and software tests should the intended host be enabled for physical testing. Preserve the distinction between declared and physically verified targets in COMPATIBILITY.md.

## 4. Input device and console setup

- Connect the existing input device to the local PC network. Confirm Wi-Fi/LAN, real device responses and manual IP fallback; do not confuse a route with a console handshake.
- ADV: test PC USB-storage/manual File setup without replacing M5Launcher, two other firmware images or Wi-Fi credentials. Start the app, join Wi-Fi and move the USB data cable to Switch. Test capitalization, punctuation, cancellation and lost-contact behavior.
- ARM: test authenticated SSH, API 1 compatibility, board USB gadget readiness and stop/follow of an existing job. Updating the panel should not force reinstalling a compatible healthy board agent.
- On a qualified host, use a fresh SmileBASIC setup screen and First Setup. Verify the expected saved file (`NS2IZM` for current IZ2S Core; not old `NS2I`). Restart the console fully and open fresh Direct before Launch.

## 5. Launch, modules and files

Test both launcher styles separately with fresh console starts. Include FS disabled and enabled. Confirm GPU checks, startup progress, the running menu and live input; preserve the first useful failure instead of replaying an unknown native operation.

Open Super File Bros. as an IZ2S tab, close/reopen its view, stop/restart its service, navigate Back/Forward/Up/breadcrumbs and check responsive actions. After qualified SD access, verify common module directories are created once and old files remain intact. Test a small disposable SD upload/download/rename/delete; internal/FTP writes must remain unavailable. Check selected external managers on their actual OS. Test both bundled File Exchange apps and Book Reader with non-private sample content. Optional modules need separate acceptance runs.

## 6. Shutdown and repeat launch

Stop a module, then the session. Confirm native/module child processes and file-transfer handles close as reported. Check disconnect, browser closure, panel reopening and server exit; an uncertain release must not be labelled successful. Fully restart the console, launch again and verify settings/shortcuts select the intended installation. Run a longer session separately; a brief 60 FPS frame-loop check is not a long-run pass.

## Result record

Keep a local table with columns **case / OS+arch / result / evidence / remaining issue**. Mark not-run cases explicitly. Update the compatibility matrix only with actual evidence. Publication remains a separate human decision after testing.
