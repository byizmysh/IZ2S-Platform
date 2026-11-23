# IZPSX emulator FAQ

## Is this an independent desktop emulator?

No. IZPSX works only through [IZ2S Panel / IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform) with a compatible launcher and confirmed console FS access. Enable Filesystem access before launch. See the [platform guide](https://github.com/byizmysh/IZ2S-Platform#readme) and [FS FAQ](https://github.com/byizmysh/IZ2S-Platform/blob/main/FAQ.md). The PC UI manages files and RAM launches; the console runs emulation.

## What does the bundle include?

The IZA installer contains the native launcher, ARM64 engine, shaders, and interface assets. The SD ZIP includes the same files in their installation folders. The release tag includes the corresponding source and licenses.

## RAM or SD?

RAM is temporary and explicitly loaded from the platform. SD is persistent and carries the native executable and its matching asset. Both use the same game/BIOS/save folders. The app detects its launch source. Initial launcher startup still needs the PC; SD operation is not a complete no-PC cold boot.

## Why must I exit a game before Connect SD card?

The native game can own filesystem and execution resources. Minus → Exit IZPSX emulator releases through its normal lifecycle before file operations. The PC file window shares the existing platform service and does not require a second Super File Bros. view.

## How do I resume an interrupted upload?

Use Continue upload and select the exact same original file. The service verifies its checkpoint before appending. A changed session, incompatible checkpoint or uncertain write can require cancelling and starting again. CUE and every referenced track must remain together. Never rename a different file to defeat verification.

## How do I return to the launcher?

Open the Minus menu and choose Exit IZPSX emulator. Plus is PS1 Start. R3 has no global exit shortcut. Follow the single-Joy-Con menu chord when using a sideways controller.

## Are save states or disc swapping supported?

No. Persistent PS1 memory cards are supported. Choose a different disc before starting the game; live disc swapping is not implemented.

## Does every game or four-player setup work?

No compatibility or performance guarantee is made for every title. Multitap must be supported by the game and the chosen port configuration. Pair controllers before entering SmileBASIC and select the game again after changing Multitap.

## Is Windows qualified?

The selected module declares Linux x64/ARM64 hosts. Windows and macOS builds are outside the 0.22 release scope. Do not broaden descriptors without integration and physical-console acceptance work.

## Can a failed native cleanup be cleared by closing the PC view?

Closing a settings view is not proof of console cleanup. If resources remain uncertain, restart the game as instructed before another native launch. The old error should remain in attempt history, not become a new error in another session.

## Has the GitHub project been published?

Yes. The project is published at [byizmysh/IZPSX-emulator](https://github.com/byizmysh/IZPSX-emulator). Download the module, SD package and corresponding source from [Releases](https://github.com/byizmysh/IZPSX-emulator/releases/latest). IZ2S Platform and confirmed filesystem access are required.

## Add your BIOS and games

BIOS and game images are **not included**. Add your own files after installing the emulator. These are paths on the Switch SD card:

| File | Exact destination |
| --- | --- |
| PlayStation BIOS, exactly **512 KiB (524,288 bytes)** | `SD:/iz2s/modules/izpsx/bios/bios.bin` |
| CUE/BIN game | `SD:/iz2s/modules/izpsx/games/<game name>/disc.cue` and its referenced BIN tracks in the same folder |
| PBP game | `SD:/iz2s/modules/izpsx/games/<game name>/game.pbp` |

Create `bios/` and `games/` if they do not exist. The BIOS filename must be `bios.bin`; keep game filenames and CUE track references consistent. Extract compressed game downloads before copying them. Do not put BIOS or games beside `launcher.iza`. Add them through **Games and BIOS** in the PC panel, FTP, or a mounted SD card. Preserve `bios/`, `games/`, `saves/` and settings when updating the application.
