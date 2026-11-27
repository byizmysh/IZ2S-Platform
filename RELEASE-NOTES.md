# IZ2S 0.3.4

## Linux panel

- The setup wizard separates PC requirements, session status and console IP settings. The selected setup step is restored after a reload. The Launch tab has a visible reinstall action with the conditions for a full setup.
- PC Link can be configured in the panel. The app library opens without a console, and search stays visible on small screens while other options are grouped under Filters.
- The app library offers verified GitHub release downloads for IZ2S Cheese, IZPSX, IZPS2, BlitBun and NX2 BreRUN, alongside local exports. The local IZPS2 export includes its tested stability defaults.

## Console launcher

- Launcher 0.34, native revision `RUN34-0.34`, is based on RUN33 v26. It draws the first menu before automatic import starts, reuses the NRO catalog when paging and refreshes the catalog after successful FTP changes.
- IZA applications support Split and Float, with a choice of audio source. NRO applications run full screen, including NX2 BreRUN 0.6.0.
- The SD package uses the SD-flex policy from RUN33. A compatible SD-flex save can be reused. When upgrading from the earlier exact-size policy, install the matching save once.

## Package

- `IZ2S-0.3.4-linux.zip` contains `install.sh` and `iz2sciv-core-sd.zip` in the same folder. The SD package includes IZ2S Cheese, IZPSX, Foldrift, NX2 BreRUN and their licenses. The panel also serves the SD ZIP as an individual download.
- The old standalone CivSwitch engines and VacooTube are removed from the distribution. The current Civilization VII module contains the host support it requires.

Linux x64 installation, local panel interactions and archive integrity were checked on a PC. Console Load Game and NX2 BreRUN launch need testing on the console. Linux ARM64 execution is a separate check.
