# Switch 2 FS demo

## Load IZ2S core

Note: You need an SD card plugged in for IZ2S core, either formatted FAT32 or EXFAT.

You are not required to move any files to your SD card.

1. Use 0.25's bundled `runtime/package` directory. It contains
   `ro/main`, `core/main`, `game/main`, `bootstrap.bin`,
   `iz2s/izlauncher.iza`, `iz2s/izlauncher.romfs`, and `programs/`.
   The IZ2S runtime is embedded in `game/main`; no launcher NRO is needed.
2. Complete the [one-time SmileBASIC setup](#one-time-setup), then open
   SmileBASIC and enter Direct mode. Leave it open.
3. From this `ns2jb` directory, run:

   ```sh
   node core.cjs --dir ../package
   ```

   If the computer has multiple network addresses, add the IP address the
   Switch can reach after the directory path. Follow the printed SmileBASIC
   instructions, including `LOAD`, `RUN`, `REMOVE`, and `V=START(...)`.
4. Wait for `IZ2S launch completed!` and the IZ2S PID before leaving SmileBASIC.
   Press HOME and close SmileBASIC normally.
5. Hold **R** while opening a native Switch 2 game. On a fresh setup, the
   first launch captures the game's policy and opens the game normally.
   Close the game from HOME, then hold **R** and launch it a second time to
   open `/iz2s/izlauncher.iza`. If a game policy was already saved, IZlauncher may open on the
   first R-held launch.

You need **SmileBASIC 4.4.9 (and now the demo version works as well!)**, **system software 23.0.1**, **Node.js 22+** on
macOS or Linux, and over **20 MiB free** in the SmileBASIC save. The computer and
console must be on the same reachable network. Ethernet has the most recorded
successful runs; acquisition has also been tested over Wi-Fi.

Notes:

- Wi-Fi acquisition is timing-sensitive and can fail on a particular run or
  console even when the host and console can reach each other.
- Keep the game on internal storage of the console, not SD.
- FS exploit is still very unstable. At best you might get a 20% success rate.
- This repo isn't intended to use as an API to build other apps on. Use it for
  debugging only.

## One-time setup

In SmileBASIC’s **Make a Program**, enter `bootstrap.bas` into slot 0 and save it
as `NS2`. A USB keyboard helps. The ZIP is a computer package, not a SmileBASIC
import file; there is no published download key.

For the demo version, you have to use code to save the file in direct mode:

```basic
PRGEDIT 0:C$="":FOR I=0 TO PRGSIZE(0)-1:C$=C$+PRGGET$() :NEXT:SAVEV "TXT:NS2",C$
```

## Run

1. Restart the console, open SmileBASIC, and enter Direct mode.
2. On the computer, open a terminal in the extracted folder and run:

   ```sh
   node run.cjs
   ```

   If asked, select the computer address reachable by the console. You can also
   supply it directly: `node run.cjs 192.168.1.10`. The connection uses port 39380.
3. In SmileBASIC, load the saved program and enter these commands separately:

   ```basic
   LOAD "TXT:NS2"
   RUN
   REMOVE NSR,&H60000000,&H3FFFFFFF
   ```

   Confirm the load. The **Out of memory** message from `REMOVE` is expected.
   Then enter the **`V=START(...)`** line printed by the computer.

   If SmileBASIC says **Wrong number of arguments**, your saved `NS2` may be
   the older nine-argument bootstrap. Use the second `V=START(...)` line the
   computer prints: it has the same port and four IP numbers, followed by
   `,0,0,0,0`. Do **not** repeat `RUN` or `REMOVE`; enter just that replacement
   `START` command while the computer is still waiting. The `bootstrap.bas`
   supplied with this repo uses the normal five-argument form, so alternatively
   you can rewrite the bootstrap.

Keep SmileBASIC open and the computer awake until **FS execution verified**.
Restart the console when finished; that also restores the original RO process.
This demo does not install custom firmware or persistence.

## Optional held RPC

To keep the same SmileBASIC-backed RPC available after the 4/4 checks, start a
fresh run with `node run.cjs --rpc [computer-IP]`. If you use the local keyboard
launcher, run `./connect-upstream.sh --rpc [computer-IP]` from its parent folder.
The normal run still closes its RPC after verification.

Once 4/4 is verified, the runner prints its `runs/switch2-*` folder and stays
open. In this mode it keeps both the SmileBASIC RPC and the existing FS memory
endpoint alive; it does not retire the FS worker after verification. The FS
instruction changes have already been restored, and the temporary RO donor
files have been cleaned up. In another terminal, check the connection with:

```sh
node rpc.cjs runs/switch2-RUNID status
```

The client also accepts one JSON bridge request, for example
`node rpc.cjs runs/switch2-RUNID '{"command":"query","address":4294967296}'`.
This is the existing native RPC, not a new process or unrestricted FS API.
It can read or change live process memory; incorrect calls can crash the app or
console. Requests are serialized, and a timed-out native request must not be
replayed because its outcome is uncertain. The bridge listens only on the PC's
loopback interface; it has no authentication, so use a trusted PC and stop it
when finished.

Keep SmileBASIC open. This RPC does **not** survive fully closing the app or a
console restart. Press Ctrl-C in the runner terminal when finished; only then
does the runner retire the FS endpoint and close the bridge and console
connection. A disconnect also ends the held run and attempts cleanup. A failed
or interrupted cleanup may require a console restart. Tools that use the FS
endpoint must finish their requests before the held run is stopped.

If acquisition fails before IZ2S is loaded, keep the `runs` folder, close and
reopen SmileBASIC, and retry on the same console boot. Enter `REMOVE` only
once per application process. Restart the console if the UI or process cannot
be closed normally, or if an already loaded IZ2S must be replaced. Logs and
temporary memory captures stay in `runs`; they are not part of the
distributable.

## Optional system-code dump

To capture installed system-program code from your own console, use this
command **instead of** `node run.cjs` for a fresh attempt:

```sh
node dump-firmware.cjs [computer-IP]
```

Follow the same SmileBASIC steps above. The command first completes the normal
4/4 exploit and verifies that the temporary FS instruction changes were
restored. Before the runner closes its RPC, it asks NCM for the installed,
full system-program IDs and reads each accessible CodeFS `main.npdm` and `main`
file. If the dump completes, it then performs the normal exploit cleanup. You
do not need to race to start a second command after 4/4; the ordinary `run.cjs`
closes its RPC at the end. `node run.cjs --dump-firmware [computer-IP]` is
equivalent. You can combine `--dump-firmware --rpc` to hold the RPC after the
dump finishes.

The output is in the repo-root `dumps/` directory. Open
`dumps/firmware-dump.json` for the title inventory, per-file SHA-256 hashes,
and entries whose CodeFS was unavailable. Captured files are in
`dumps/<title-id>/` directories, alongside `dumps/ncm-system-programs.bin`.
Starting another firmware-dump run clears the previous `dumps/` contents;
ordinary `run.cjs` runs do not touch them. An unrecognized nonempty `dumps/`
directory is never cleared automatically. Copy any partial or completed dump
you want to keep before starting another dump run. Run logs remain in `runs/`.

At startup, the runner checks the roots of SmileBASIC's inspected `save` and
`tmp` mounts and deletes every file or directory whose name starts with
`NS2_GAP`. Directories are deleted recursively. This no longer depends on
previous run reports or checks the entry's contents, so do not keep personal
files with that prefix on either mount. Other names and mounts are untouched;
the runner does not wipe all of `tmp`.

The metadata probe now selects a 16 KiB or 4 KiB window from the observed
allocator link, and the later layout check validates the captured SDK page
while discovering the domain vtable independently. These paths matched saved
hardware captures from both observed layouts.

Physically tested on October 21, 2026: both FS instruction challenges,
restoration, and cleanup passed. Acquisition is timing-sensitive: the test
passed on the third fresh boot. The first two attempts stopped at validation
checks and cleaned up.

## What this fork improves

This fork aims to make the exploit work across the two Switch 2 memory layouts
we observed before and after installing the microSD Express support update. The
original could get through the initial exploit, then fail while starting its
native helper; this fork adapts to either observed layout.

Supports demo version.

To check for the microSD Express support update, open **System Settings →
Support/Health & Safety** and look at item 3. On our test console it showed
**(B)** after the update. If yours shows **(B)**, use this fork: the original
fixed-layout version may fail on the layout we observed after the update. This
fork also supports the layout we observed before the update, so you can use it
even if item 3 does not show **(B)**.

It also reduces some avoidable failures: it spends less time searching memory,
gives timing-sensitive steps more bounded chances to succeed, accepts a valid
memory pattern the original rejected, and handles leftover temporary files
from interrupted runs. A free-space check that could reject exactly enough
space is fixed, and failures produce more useful diagnostics.

These changes improve compatibility and recovery, but the exploit is still
timing-sensitive. They are not a guarantee that every attempt—or every
console—will succeed.

The host now waits up to 3 minutes of inactivity for a native-bridge request
and up to 2 minutes for each console response after pairing. The initial
console greeting still has a 10-second limit. These longer waits can avoid
aborting a slow request that would work otherwise.

Runs two small native functions inside FS, checks their results, and restores the
original instructions. Switch 2 only; no npm install or compiler needed.

## Code

`run.cjs` is the host entrypoint. `engine/switch2/pipeline.cjs` contains the fixed
launch sequence; `engine/switch2/process/fs-execute.cjs` contains the FS demo.
The native helper and its source are in `engine/switch2/ro/payload/`.
Developers can reproduce it with Zig 0.15.2:
`node engine/build-payload.cjs --check --zig /path/to/zig`.
