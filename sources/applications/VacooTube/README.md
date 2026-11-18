# VacooTube

<p align="center"><img src="assets/icon-master.png" width="120" alt="VacooTube — a glowing video lamp"></p>

VacooTube is a video-player project for **Switch 2 / NS2**, running on a Linux PC and controlled from the console through **[IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform)** and an ABI-1 launcher (IZlauncher, IZPSlauncher or IZ2Launcher). It uses a console/TV-style YouTube interface with touch and controller input.

**This is a fork of [VacuumTube](https://github.com/shy1132/VacuumTube), adapted for NS2 and IZ2S.** The application source is included in `app/`; original copyright/MIT terms are preserved in `app/LICENSE`, and source revision/changes are recorded in [THIRD_PARTY.json](THIRD_PARTY.json).

This is not a native standalone Switch executable, a new console connection engine or a console emulator. The desktop application executes on the PC; the existing console session presents its video/audio and supplies input.

## A quick look

<table align="center">
  <tr>
    <td align="center" width="50%">
      <a href="docs/images/navigation.jpg"><img src="docs/images/navigation.jpg" width="420" alt="VacooTube home screen with its left navigation menu"></a><br>
      <sub><strong>Familiar navigation</strong><br>Use the left sidebar to browse the home screen and player sections.</sub>
    </td>
    <td align="center" width="50%">
      <a href="docs/images/search-results.jpg"><img src="docs/images/search-results.jpg" width="420" alt="VacooTube showing search results for cats being dramatic"></a><br>
      <sub><strong>Browse videos</strong><br>A TV-style interface with large cards and familiar navigation.</sub>
    </td>
  </tr>
</table>


## Streaming engine

VacooTube uses **BlitBun** as its streaming engine for frame encoding, packet compression and video/audio delivery to Switch 2. The required engine implementation is bundled inside VacooTube under `server/streaming/`; installing the separate BlitBun module is not required. Each project can be installed and updated independently. [IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform) is the required host platform; this repository contains the player and its bundled streaming integration.

## How it works

1. IZ2S establishes its normal console session and loads the module.
2. Opening VacooTube on Switch starts the owned PC application on demand.
3. The app creates an offscreen Electron page, without a visible desktop game/player window or focus stealing.
4. The private capture/input bridge sends fresh video and application audio through its own bundled streaming engine. Touch/controller events return to that owned page.
5. Exiting the app/session closes owned processes, pipes and its silent audio sink. Merely installing files does not start a background player.

The module keeps private browser/account state under its own `data/profile/`. It does not sign you in, accept consent screens or clear online account history automatically.

## Requirements

- Linux x64 or ARM64, IZ2S Platform 0.16.0-rc.3 or newer, API 1, console ABI 1 and IZlauncher.
- A working existing Switch 2 / SmileBASIC connection through the platform. This project does not install a bootstrap itself.
- PipeWire/PulseAudio compatibility, `pactl` and `parec` for the owned silent audio path.
- Node.js/npm and Python 3 to prepare a source installation. The pinned desktop runtime uses Electron 44.5.1.
- Network access for YouTube itself. A ready offline installer removes dependency downloads, not the website's internet requirement.

See [supported hosts and validation limits](SUPPORT.md). Windows and macOS console streaming are unsupported. Linux is the implemented streaming target. Desktop build metadata for other platforms is retained as source, but Windows/macOS console-streaming audio backends and physical ARM64 playback are not claimed tested.

## Install

IZ2S Platform 0.25 includes the VacooTube server, Electron for Linux x64 and ARM64,
and prepared application dependencies in this module. Install VacooTube on the
console from the App library or Add App. Opening it on the console starts the
PC source automatically while IZ2S Platform is running. A website connection is
required to play online video.

First install [IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform/releases/latest). VacooTube is independent: BlitBun is not required. Download the Linux x64 offline package or the source package from [Releases](https://github.com/byizmysh/VacooTube/releases/latest), then extract it. From this folder:

```sh
IZ2S="/absolute/path/to/iz2s-platform"
./install.sh --platform "$IZ2S"
```

The module ID is **`vacootube`**. All application, server and runtime files are installed under **`modules/VacooTube/`**. `--replace` migrates the former `youtube` installation and preserves its private data and a backup.

## Install on Switch 2

1. **Through IZ2S:** Connect your Switch 2 to IZ2S Platform on the PC. Open **App library → Add app** and select `vacootube-1.5.7.iza`. Open **Add App** in IZ2S on the Switch to receive and install the file.
2. **Copy to the SD card:** Open the card on your PC. Create `iz2s/modules/vacootube/` at the root if needed. Copy `vacootube-1.5.7.iza` there and rename it to `launcher.iza`. The final path is `SD:/iz2s/modules/vacootube/launcher.iza`. Reinsert the card and open VacooTube from the IZ2S menu.

### Source-only installation

For a source-only installation outside the 0.25 distribution, prepare the runtime manually:

```sh
node "$IZ2S/modules/VacooTube/server/local-package/install.cjs"
```

The installer prepares the included fork source and its locked production npm dependencies, with npm lifecycle scripts disabled. It downloads **official Electron**, verifies its published SHA256 inventory and uses that runtime. It does not download another player build or auto-update over your fork.

A source installation needs internet access for missing Electron/npm files. The runtime installer leaves the user profile untouched. If a compatible prepared runtime is already present, it reuses it.

### Ready offline Linux x64 release

Extract the architecture-labelled offline archive, which includes the player runtime and prepared application dependencies. Install it with:

```sh
./install.sh --platform "$IZ2S" --with-runtime
```

Use an archive matching the host architecture. The installer preserves existing module private data. A source Git checkout deliberately excludes the bulky downloaded runtime and `node_modules`; they are supplied as release assets or prepared locally. All application/integration source files are in this repository.

### Intentional update

Stop the affected console session first. Add `--replace` when replacing an installed module, and `--with-runtime` when supplying the matching offline runtime. The previous complete module is kept under the platform's private `data/module-backups/vacootube/`. No console keyboard commands are sent by this file installer.

## Start and controls

After the initial platform and module setup, **all player controls are handled on the console**: browsing, search with the on-screen keyboard, playback, settings and returning to IZlauncher. The PC runs the application and streaming in the background; no PC keyboard or mouse is needed for normal player operation.

Select/load **VacooTube** in IZ2S App library, then open VacooTube on Switch. The separate Controllers module is not required just to navigate this player: its input goes directly to the owned page, not through a virtual PC gamepad.

- **Touch:** native touch start/move/end events for navigation, scrolling and the on-screen keyboard.
- **D-pad / left stick:** navigation.
- **A:** confirm; **B:** back; **X:** play/pause.
- **Double-tap Minus:** open the hidden module menu to resume, change render resolution or render engine, or return to IZlauncher.

First-run/account/consent pages appear on the Switch. Handle them yourself. The host bridge does not require a public debugger/HTTP input port; it uses private inherited pipes scoped to the owned app.

### Going back and returning to IZlauncher

- **B on Switch** sends **Escape** to the player: it goes back one screen or closes the current view. It is not the keyboard letter `b`.
- **On-screen Back arrow** provides the same visible way out of the search view.
- **Home** in YouTube's left sidebar returns directly to the player home screen. Back is history-based and may require more than one press.
- **Double R3** returns to IZlauncher. **Double Minus** opens the player menu. Back inside YouTube does not close the module.
- **Backspace** is context-dependent and can delete search text; do not rely on it as a universal Home command.

## Video and audio modes

The app renders its interface at **720p by default**. Double-tap **Minus** to open the otherwise hidden console menu, then choose **Render resolution**: 1080p, 720p, 540p, 480p, 360p or 240p. Changes resize the actual offscreen surface and take effect without restarting the app. Touch coordinates follow the selected surface. BC1 requires dimensions in multiples of four; 480p uses 852×480 and 240p uses 428×240. Video content defaults to **480p** when available; the preference is applied once per video, so a subsequent manual quality choice can remain. Source-video quality, interface surface and BlitBun's general game output settings are different values.

The offscreen compositor is configured up to 240 FPS rather than imposing a 30/60 FPS application cap. Actual displayed rate still depends on the content, paint demand, transfer and console. There is no guarantee of 240 FPS or a minimum console frame rate. Ordinary performance HUD and the player's touch-arrow overlay are disabled in the managed mode.

The app uses an **owned silent virtual sink**. Its sound is captured for Switch while staying inaudible on PC speakers; the system default audio device is unchanged. Audio capture is independent of video requests. Missing `pactl`/`parec`, a failed sink or the console's output choice can still prevent sound.

Capture prioritizes fresh compositor output, uses bounded request state and pauses/resumes around stream route transitions. Video recovery requires the main IZ2S native session to remain valid. A terminated ordinary-bootstrap main session still needs a fresh console launch.

## Files in this repository

| Path | Meaning |
| --- | --- |
| `app/` | Complete fork application source, lockfile, assets and original MIT license |
| `server/` | Module integration, touch/input, audio and capture bridge |
| `server/streaming/` | Bundled streaming implementation; no other installed module is required |
| `server/local-package/` | Runtime installer, source preparation and offscreen launcher |
| `switch/` | Console contribution, shaders and branded icon |
| `assets/` | Icon master and generation/design notes |
| `module.json` | Console/server requirements, inventory and bundled BlitBun engine |
| `tools/`, `install.sh` | IZ2S-aware check/package/install/release commands |
| `runtime/` | Prepared Electron and source dependencies; excluded from the source package |
| `data/` | Private player preferences and account profile; excluded from distribution |
| `dist/` | Generated source/offline release archives and checksums |

## Develop and package

Edit `app/` to develop the player itself; edit `server/` for console integration. Do not edit cached `runtime/source/<hash>/` or a live `.versions/` snapshot as your source of truth. Re-run the runtime installer after application-source edits so the prepared dependency/source snapshot matches the current fork.

```sh
node tools/module.cjs check --platform "$IZ2S"
node tools/module.cjs pack --platform "$IZ2S"
# Commit finished source changes before release:
node tools/module.cjs release --platform "$IZ2S"
node tools/module.cjs release --platform "$IZ2S" --with-runtime
```

The source installer contains the PC and console source. Runtime binaries, npm dependencies and personal data are excluded. Only its declared console files are uploaded to Switch. Large dependencies belong beside their module, not in the console payload.

Updates are managed as versioned IZ2S module releases. The fork does not check/install the original application's update feed. Project/help buttons open this repository; manual download actions open its Releases page. Updates remain deliberate module installations, not automatic application replacements.

[Installation and troubleshooting](docs/OPERATIONS.md) · [Application source guide](app/README.md) · [License inventory](THIRD_PARTY.json).

## Independent installation

VacooTube bundles the capture/codec/transport implementation it uses under `server/streaming/`, adapted from the IZ2S BlitBun source. It does not import an installed BlitBun module and declares no dependency on it. The two modules can be installed individually or together. IZ2S serializes ownership of the console video slot when switching between them. This avoids duplicate active writers while keeping each module self-contained. Electron/system audio libraries remain external system dependencies, not module dependencies.

## Rendering engines

Open the hidden stream menu by **double-tapping Minus**, then choose **Render engine**:

| Engine | How it works |
| --- | --- |
| **Classic** · default | WASM BC1 encoding with one frame prepared ahead, changed-block compression and a persistent connection. |
| **Advanced** | Native BC1 encoding when available, with a WASM fallback, plus compression prefetch and a dedicated transfer pipeline. |

Both engines are bundled with VacooTube and work without a separately installed BlitBun module. Choose the one that works best on your PC and network; the names do not imply a guaranteed frame rate.

Switching engines keeps the player, audio and selected resolution open. The previous renderer and connection are stopped before the replacement starts, and the picture remains black until a fresh frame is ready. If the old connection cannot shut down safely, a second renderer is not started.

The same menu offers **Continue**, **Render resolution** and **Return to launcher**. Rendering defaults to **720p**, with choices from 1080p down to 240p. YouTube video quality is a separate setting, defaulting to 480p when available. Preferences are saved in `modules/VacooTube/data/render-settings.json`; existing saved engine selections remain compatible. IZ2S Platform 0.16.0-rc.3 or newer is required for the console menu.

See the [rendering guide and dependency map](docs/render-engine.md) for implementation details.

## Project license

Independently authored IZMYSH code is licensed under **GPL-2.0-or-later**.
See [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT) and [license scope](LICENSE-SCOPE.md).
Existing component licenses and prior MIT grants remain applicable; this is not
a blanket relicensing of third-party material.

## Standalone console entry — 1.5.0

VacooTube now owns its console shaders, fixed ASCII font, menu and 2 MiB private video allocation. It runs as an ABI-1 PC-managed application in IZlauncher, IZPSlauncher or IZ2Launcher; it is not a shell contribution. Cosmetic launcher updates do not rebuild this entry.

Double **R3** exits to the launcher. Double **Minus** opens/closes the menu. Single R3 does not exit; a second press after 600 ms starts a new gesture. Waiting/error screens stay visible when fresh video is unavailable.

Install to **RAM** for the current session, or choose **SD** to preserve the package. Both modes require the connected PC for capture/player execution. SD storage does not make streaming autonomous. A new platform session is needed after this update; updating files does not replace code in a running pinned session.

Build the owned entry with `node tools/build-switch.cjs /path/to/IZ2S` before packing. From the platform root, `npm run check:blitbun` validates both streaming modules and executes their real GPU tests. Physical console audio/video synchronization still needs hardware verification.

### IZ2S Platform 0.22 installer

Build a source installer with `node tools/build-installer.cjs /absolute/path/to/iz2s-platform`. The archive in `dist/` contains `install.sh`, the verified IZM and file checksums. Use the command in Source-only installation to prepare or repair the runtime.

## Streaming audio — 1.5.3

Stereo capture now requests stereo output on HDMI as well as handheld outputs. PCM blocks start at 64 ms and adapt to sustained refill round-trip time, with at most two device buffers (320 ms maximum) and a bounded live source queue (200 ms maximum). This avoids a growing audio backlog. Single scheduling spikes do not increase buffering. Released-buffer ownership checks remain in place.

Video ACKs already contain controller input; RPC input fallback begins only after 100 ms without an ACK. Audio routing is observed on startup or an actual route signal, without periodic native polling. Diagnostics now include missed refill deadlines even when an expired descriptor has not yet been reclaimed.

Update the module in the App library, including the SD copy when installed there, and start a new launcher session to replace previously pinned host code. Running streams are not patched in place.
