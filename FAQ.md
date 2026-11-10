# IZ2S Platform 0.22 FAQ

## What does this release contain?

IZlauncher, IZPSlauncher, IZ2S Core, the BermTheSequel alternative engine, input-device support, File Exchange, File Exchange FS, Book Reader, integrated Super File Bros. and IZPSX emulator 0.5.0. Other emulators, streaming apps and test builds are not bundled. See [README](README.md#included-applications).

## Which PC systems are targeted?

Linux x64/ARM64 only. Windows and macOS packages are outside the 0.22 release scope. Per-component host declarations still apply to native console sessions and modules. See [Compatibility](COMPATIBILITY.md).

## How do I install and open the panel?

Extract every file, open `Install-Linux.desktop` or `Install.cmd`, and follow the OS prompts. On Linux, `sh install.sh --launch` is the terminal alternative. Start the installed app later with its shortcut or `start.sh`/`Start.cmd`. Do not run from inside the ZIP. Installation may download verified runtime dependencies; it does not start a console session.

## What screen must SmileBASIC show for Launch?

For a fresh launch, fully restart the console as instructed, open **SmileBASIC 4 → Make a Program → Direct**, and leave a fresh Direct screen open. Choose Launch in the panel, confirm the screen and press Load IZlauncher. First Setup installs the BASIC source/save; it is a different step. A failed attempt may require reopening the game even if a new attempt is selected on the PC.

## What is the connection engine?

IZ2S Core and ns2jb by BermTheSequel provide the SmileBASIC connection. IZlauncher/IZPSlauncher are console menu styles. Neither launcher choice is a RAM-app selection. Civ7 offers a Wi-Fi LDN or USB delivery engine for its initial setup, followed by TCP-based launch.

## Does Civ7 Wi-Fi also support LAN?

Yes. LDN first setup uses a PC Wi-Fi adapter, but later Launch can use **LAN or Wi-Fi** on the same local network. The two icon buttons on the PC and Console cards select the same intended console transport. They do not reconfigure the PC's operating-system network interface. Use the corresponding address if entering an IP manually.

Open **Load Game** on Switch, wait for **Awaiting payload**, then choose Connect and start launcher. The PC can be ready while the Console card still waits for that loader; these are separate states.

## What do I need for LDN first setup?

Native Linux, the supported Civ7 game/version, a usable Wi-Fi radio, helper dependencies, your own compatible Switch 1 `prod.keys`, and helper network permissions. Step 1 displays the command to copy into a terminal. Ethernet can remain connected while the selected Wi-Fi radio is used for LDN. Later TCP launches do not need the LDN radio or keys again. Follow the wizard if Wi-Fi restoration fails; do not blindly repeat delivery into a changed game screen.

## When is the Civ7 save installed?

Only when the save-install option is selected and the required backup/deletion consent is confirmed. Installation replaces ALL single-player saves in the named game save directory and writes `IZ2S.Civ7Save`. Back up needed saves first. The result view reports installation separately from loader connection. Later use Load Game and Launch; chat delivery need not be repeated.

## What is Filesystem access? Why can it fail after a restart?

It obtains the filesystem service needed by many apps and SD operations. The UI's 30–40% estimate is per attempt, not a promise. A full restart resets the game's state but cannot guarantee that acquisition succeeds. A new FS failure is not evidence of an old PC lock.

If native cleanup or memory placement is uncertain, resources are retained and automatic retry is disabled. Close/restart the game as instructed before a fresh attempt. Never clear that condition just by hiding an error or repeatedly pressing Launch. Old finished attempts stay in history and are not replayed as new error notifications when the panel reopens.

## Why did my RAM apps carry across another game before?

0.22 requires explicit selection. Enable **Load modules at startup · RAM** and choose the apps, or load them directly from the App library. The previous RAM selection is not restored automatically. RAM and SD are independent; an installed SD app remains visible until removed.

## How do I find installed apps?

Use installed/not-installed and SD/RAM filters in the App library. Each card shows observed installation badges, install/load actions and settings. The Connection page shows a compact inventory after connection; opening a module from there provides a return action to Connection. PC package availability alone does not mean an app is already installed on SD.

## Where is Super File Bros.?

After connecting, use **Files & uploads → Open file manager**, beside the session card. It is the integrated browser manager for uploads and console folders. The entry stays hidden before connection. FTP controls are optional external-file-manager access, not a prerequisite for the browser file list.

## Can IZPSX and Super File Bros. share the file service?

Yes. Their views share the same endpoint and ownership rules; opening another window is not required. A running native game or uncertain cleanup can still own resources. Exit IZPSX through its menu before connecting SD for file operations. File-service windows do not grant permission to overwrite a running game's files.

## Uploads and resume

The panel shows transfer phase, current file, byte progress and cancellation. An interrupted resumable upload keeps a checkpoint. Select **Continue upload** and choose the exact same original file; the checkpoint is verified before appending. A different file, changed session, unknown write outcome or legacy protocol can prevent safe continuation. In that case cancel the unfinished transfer before starting again. A cancelled upload must clear its visible progress state; a partial file is not a finished installation.

For PS1 discs, upload a complete game folder. Keep CUE and BIN/IMG track names together. The IZPSX view validates references before sending, sends tracks before the CUE and does not include cover/readme extras. Use your own BIOS and game dumps; none are bundled.

## Can I minimize apps into the launcher?

General suspend/resume is not implemented. Returning to the launcher ends an app through its lifecycle/cleanup path. Do not assume background native execution is safely suspended. Unconfirmed cleanup is reported separately and may require restarting the game.

## Is Plus a global exit key?

No. The current launcher does not use Plus as a global reconnect/exit trigger. Apps can use it for their own controls. IZPSX uses Plus for PS1 Start and Minus for its own menu. Other apps define their own shortcuts; follow their displayed hints.

## Can I play after disconnecting the PC or sleeping Switch?

Some console-local SD apps can continue after initial startup. This is not a standalone cold boot without the PC. File services, host-controlled changes and streaming require a connection. Resume/recovery depends on the retained game's process and the available recovery service. Use Restore connection only when offered; it does not create a fresh BASIC acquisition.

## What does Attempts show?

Successful and unsuccessful attempts, with result, time and diagnostic information. Filters distinguish result, date and error category; counts help inspect today's failures. You can delete selected attempts or clear history through the provided controls. History removal does not clean native resources or restart the console.

## How do I stop a session or close a settings view?

Stop session ends the PC-controlled session. A module's Stop control ends that module/settings process; Close view only dismisses the view. An unavailable or already exited process must not keep the tab permanently uncloseable. If native cleanup remains uncertain, closing the view does not prove the console is safe for another native app.

## Are personal files or credentials included in the release?

No. Runtime `data/`, previous sessions, pairing credentials, keys, logs, user books, BIOS, game files and memory cards are excluded. Public checksums describe shipped code/assets only. Do not attach those private directories to public issues.

## Where are the IZPSX source and separate release?

The bundled module is 0.5.0. Its matching corresponding source is in `sources/izpsx`. A separate local Git repository and source/module archives are prepared alongside this platform candidate. No new GitHub repository has been created or published as part of this preparation. That standalone repo's README, build instructions and license describe its own scope.

## How can I report a problem?

See [SUPPORT.md](SUPPORT.md). Include the platform/module version, PC OS and architecture, game/version, selected engine/menu and whether the app is in RAM or on SD. Export only the relevant attempt log after reviewing it for private information. A local automated pass is not physical console qualification.
