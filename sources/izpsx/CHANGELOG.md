# IZPSX emulator 0.5.1 — controller handoff update

- AUTO Player 1 prefers attached handheld Joy-Con even while a wireless Pro Controller remains connected, and returns to the first external controller when handheld input disconnects.
- Repeated dock/handheld changes update both game and menu input. Explicit device assignments, Off and Players 2–4 keep their existing behavior.
- Regression coverage includes Pro staying connected, paired Joy-Con, neutral disconnection and handheld reserved for another player.

# IZPSX emulator 0.5.1

First standalone GitHub release: module, SD installation package and complete corresponding source. Requires [IZ2S Platform](https://github.com/byizmysh/IZ2S-Platform) and confirmed FS access.

- Updated to the checked 0.5.1 native SD consumer using public ABI services and compatible suffix extensions, without private launcher-owner layouts.
- Documented the mandatory IZ2S Panel and FS session requirements, detailed app features and reproducible native UI screenshots.
- Frozen IZM and matching native engine selected for IZ2S Platform 0.22.
- RAM/SD launch-source detection, persistent SD game/BIOS folders and memory-card generations.
- Shared platform file-service integration and game-folder/CUE validation in the PC settings view.
- Per-player controller settings, sideways Joy-Con layouts and game-specific Multitap selection.
- Plus is PS1 Start; Minus opens the menu/settings; choose Exit IZPSX emulator to return.
- Public source tree includes the corresponding PCSX-ReARMed snapshot, ABI profile, licenses and portable build/packaging entry points.

See COMPATIBILITY.md for the limits of local checks. The preparation does not add emulator features or assert new physical-console qualification.
