# Frequently asked questions

[English](FAQ.md) · [Русский](FAQ.ru.md) · [简体中文](FAQ.zh.md) · [Español](FAQ.es.md)

[Русский](FAQ.ru.md) · [Full installation guide](docs/INSTALLATION.md)

## 1. What is needed for the first installation?

A Nintendo Switch 2 on firmware **23.0.1**, the **Nintendo Switch 1 cartridge of Civilization VII 1.0.0**, a microSD card, native Linux with a compatible Wi-Fi adapter, and your own Switch 1 `prod.keys`. Select English for the console interface and English (US) for USB keyboard input before opening the game.

## 2. Which console languages should I select?

Choose English for the interface and English (US) for USB keyboard input before launching the game. The paths are `HOME → System Settings → System → Language → English` and `HOME → System Settings → Controllers & Accessories → USB Keyboard → English (US)`. This edition includes the PC panel only for native Linux; Windows and WSL are not supported release hosts.

## 3. Where should I extract the SD archive?

Extract `IZ2S-0.3.5-linux.zip` on your PC and find `iz2sciv-core-sd.zip` next to `install.sh`. Extract the SD archive to the card root so the paths begin with `SD:/iz2s/`. You can download another copy from the panel. Merge folders and replace matching package files when updating. Keep `Nintendo`, other apps, BIOS, games, saves and settings. Preserve the included license files and safely eject the card.

## 4. When should I repeat the full procedure?

Reuse a compatible save after updating. Repeat the full procedure when the save needs replacing, when switching from another loader, or after more than seven unsuccessful launches. In step 3, clear all Civilization VII save data for the selected profile in `HOME → System Settings → Data Management → Delete Save Data → Civilization VII → Your profile`.

## 5. Do I install the save manually after reconnecting?

No. **Connect and install save** restores PC Wi-Fi, verifies the loader and automatically writes `IZ2SCIV.Civ7Save`. Leave Awaiting payload open until the operation finishes. If installation fails, the panel offers a retry; it does not treat an unconfirmed write as success.

## 6. Will the automatic installer remove my other game saves?

Automatic installation replaces only `IZ2SCIV.Civ7Save`. When repeating the full procedure, clear all Civilization VII save data for the selected profile in System Settings at step 3.

## 7. The console IP was not found. What should I do?

Expand the IP settings directly under **Session status**. Search and check a proposed address or enter the IP displayed by the console. Keep Awaiting payload open and ensure PC Wi-Fi has been restored. FTP availability alone does not prove that the game loader is ready.

## 8. How do I launch after installation?

Return to HOME, close Civilization VII with X, reopen it and choose Load Game without rebooting the console. Follow the panel until IZ2S CORE IS RUNNING, close Civilization VII, then hold R while opening a Switch 2 game. Repeat the R-held launch once if required. Full failure/retry instructions are in the [guide](docs/INSTALLATION.md#4-launch-iz2s).

## 9. How do I configure PC Link or FTP?

For PC Link, open **Launch → Configure PC Link**, enter the console’s local IPv4 address and click **Start PC Link**. Download `pc-link.cfg` and save it at `SD:/iz2s/pc-link.cfg`. Reopen IZ2S, then press **R + L3**. The PC and console must be on the same local network. If another IZ2S panel is using PC Link, pause it first.

For FTP, press **L + R3** on the console, then **Connect FTP** in the panel.

## 10. Why does the launcher sometimes start slowly or show sd-verify?

A new shader/driver cache can require compilation. This revision fixes a cache-replacement failure that previously repeated compilation at every launch. Preserve the local cache; do not distribute another console's cache. An sd-verify error means the expected runtime and installed files must be compared by size and SHA-256, including the launch save's manifest. Use one matching package and reinstall its save when required.

SD-flex: install the SD-flex launch save once. Compatible launcher, menu and ROMFS updates then require only their SD files, even when sizes change. A changed bootstrap, ro, core or boot ABI still needs a matching save. Use complete verified update sets.

In 0.3.5, other tile artwork is deferred until after the first frame and the new graphics cache is keyed by shader contents. A real shader or driver change still needs compilation once. When upgrading from the published 0.3.4 package, the changed RO component also requires a complete matching runtime set and launch save.
