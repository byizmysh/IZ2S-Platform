IZPSX emulator 0.5.1 — @byizmysh

SD:/IZ2S/Homebrew/Modules/izpsx/
  launcher.izapp + declared asset-*.izdata
  games/ — BIN/CUE or PBP, each game in its own directory
  bios/scph101.bin — your own 512 KiB BIOS
  saves/ — two shared memory cards, two verified generations each

Source is detected automatically: session RAM or public native SD launcher ABI 5.
RAM entry needs the local PC launcher; the SD entry reads all game data locally.
About shows the current source. Games/BIOS are not included.
In-game memory card saves are supported; save states are not implemented.
Plus is PS1 Start (game menu). Minus opens the IZPSX menu or library settings.
Choose Exit IZPSX emulator in the menu to return to the launcher. R3 has no shortcut.
Settings: L/R changes tabs.
Controls: choose Player 1-4 with ZL/ZR or the player selector.
Each player has separate device, button swap, stick D-pad, deadzone and turbo settings.
Choose Off to disconnect game input; Disconnect from Switch requests system disconnection.
B saves all profiles. X resets one setting; Y resets only the selected player.
Existing shared and two-player settings migrate automatically.
Multitap: Off keeps two normal PS1 ports. Port 1 / Port 2 enables up to four players.
Choose the game again after changing Multitap. The game must support Multitap.
Pair Joy-Con halves individually using SL/SR in HOME before entering SB4.
Single Joy-Con: hold sideways. SL/SR = L1/R1; outer shoulders = L2/R2.
Stick click = Select; +/- = Start; SL+SR+/- = IZPSX menu.
For paired Joy-Con and Pro controllers: + = Start, - = IZPSX menu.
Display FPS cap preserves native game speed/audio.
Source and local qualification reports accompany IZPSX emulator.
