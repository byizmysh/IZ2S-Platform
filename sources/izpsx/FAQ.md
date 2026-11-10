# IZPSX emulator FAQ

## Is this an independent desktop emulator?

No. This is an IZ2S module and a console-native payload. It requires a compatible IZ2S launcher/SDK; the optional PC UI manages files and launches RAM entries.

## What does the bundle include?

The 0.5.0 module, matching native SD assets, source/frontend/server code, pinned PCSX-ReARMed source, ABI metadata and licenses. BIOS, commercial games, keys, user saves, crash dumps and private test captures are not included.

## RAM or SD?

RAM is temporary and explicitly loaded from the platform. SD is persistent and carries the native executable and its matching asset. Both use the same game/BIOS/save folders. The app detects its launch source. Initial launcher startup still needs the PC; SD operation is not a complete no-PC cold boot.

## Why must I exit a game before Connect SD card?

The native game can own filesystem and execution resources. Minus → Exit IZPSX emulator releases through its normal lifecycle before file operations. The PC file window shares the existing platform service and does not require a second Super File Bros. view.

## How do I resume an interrupted upload?

Use Continue upload and select the exact same original file. The service verifies its checkpoint before appending. A changed session, incompatible checkpoint or uncertain write can require cancelling and starting again. CUE and every referenced track must remain together. Never rename a different file to defeat verification.

## How do I return to the launcher?

Open the Minus menu and choose Exit IZPSX emulator. Plus is PS1 Start. R3 has no global exit shortcut in 0.5.0. Follow the single-Joy-Con menu chord when using a sideways controller.

## Are save states or disc swapping supported?

No. Persistent PS1 memory cards are supported. Choose a different disc before starting the game; live disc swapping is not implemented.

## Does every game or four-player setup work?

No compatibility or performance guarantee is made for every title. Multitap must be supported by the game and the chosen port configuration. Pair controllers before entering SmileBASIC and select the game again after changing Multitap.

## Is Windows qualified?

The selected module declares Linux x64/ARM64 hosts. Windows and macOS builds are outside the 0.22 release scope. Do not broaden descriptors without integration and physical-console acceptance work.

## Can a failed native cleanup be cleared by closing the PC view?

Closing a settings view is not proof of console cleanup. If resources remain uncertain, restart the game as instructed before another native launch. The old error should remain in attempt history, not become a new error in another session.

## Has the GitHub project been published?

Not by this preparation. This is a local repository and local set of release assets. A maintainer must choose/create the remote repository and publish after review. No fabricated repository URL is used as a download link.
