# Frequently asked questions

[English](FAQ.md) · [Русский](FAQ.ru.md) · [简体中文](FAQ.zh.md) · [Español](FAQ.es.md)

[Русский](FAQ.ru.md) · [Full installation guide](docs/INSTALLATION.md)

## 1. What is needed for the first installation?

A Nintendo Switch 2 on firmware **23.0.1**, the **Nintendo Switch 1 cartridge of Civilization VII 1.0.0**, a microSD card, native Linux with a compatible Wi-Fi adapter, and your own Switch 1 `prod.keys`. Select English for the console interface and English (US) for USB keyboard input before opening the game.

## 2. Which console languages should I select?

Choose English for the interface and English (US) for USB keyboard input before launching the game. The paths are `HOME → System Settings → System → Language → English` and `HOME → System Settings → Controllers & Accessories → USB Keyboard → English (US)`. This edition includes the PC panel only for native Linux; Windows and WSL are not supported release hosts.

## 3. Where should I extract the SD archive?

Into the SD root, so the paths begin with `SD:/iz2s/`. Merge folders and replace matching package files when updating. Keep `Nintendo`, other apps, BIOS, games, saves and settings. Preserve the included license files and safely eject the card.

## 4. I have an old IZ2S version or another loader. What changes?

Update the SD files and follow the full procedure. The conditional cleanup step uses `HOME → System Settings → Data Management → Delete Save Data → Civilization VII → Your profile`. This deletes that profile's game save data; read the step before confirming. A launch save from another runtime revision is not interchangeable with this package.

## 5. Do I install the save manually after reconnecting?

No. **Connect and install save** restores PC Wi-Fi, verifies the loader and automatically writes `IZ2SCIV.Civ7Save`. Leave Awaiting payload open until the operation finishes. If installation fails, the panel offers a retry; it does not treat an unconfirmed write as success.

## 6. Will the automatic installer remove my other game saves?

The current installer replaces only its own trigger save and confirms the filesystem commit. Deleting the profile's saves in System Settings when migrating from an older loader is a separate action that does remove that profile's progress.

## 7. The console IP was not found. What should I do?

Expand the IP settings directly under **Session status**. Search and check a proposed address or enter the IP displayed by the console. Keep Awaiting payload open and ensure PC Wi-Fi has been restored. FTP availability alone does not prove that the game loader is ready.

## 8. How do I launch after installation?

Return to HOME, close Civilization VII with X, reopen it and choose Load Game without rebooting the console. Follow the panel until IZ2S CORE IS RUNNING, close Civilization VII, then hold R while opening a Switch 2 game. Repeat the R-held launch once if required. Full failure/retry instructions are in the [guide](docs/INSTALLATION.md#4-launch-iz2s).

## 9. Why is FTP enabled on the console but disconnected in the panel?

These are separate server/client states. Enable FTP with L + R3, then select **Connect FTP** in the panel. PC Link uses R + L3 and has its own status. The actual address and port are shown on the console.

## 10. Why does the launcher sometimes start slowly or show sd-verify?

A new shader/driver cache can require compilation. This revision fixes a cache-replacement failure that previously repeated compilation at every launch. Preserve the local cache; do not distribute another console's cache. An sd-verify error means the expected runtime and installed files must be compared by size and SHA-256, including the launch save's manifest. Use one matching package and reinstall its save when required. An old RUN25 label alone does not identify the loaded build.
