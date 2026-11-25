# Install and update IZ2S for Civilization VII

[Русский](INSTALLATION.ru.md) · [Requirements and PC installation](../README.md#requirements)

This guide follows the panel's four installation steps. Use panel 0.3.2 and the matching launcher 0.32 `RUN32-0.32-Civ7-Lifecycle-Audit-1` package. Initial LDN delivery requires native Linux, a compatible Wi-Fi radio and your own Switch 1 keys. The target is Switch 2 firmware **23.0.1** and the **Nintendo Switch 1 cartridge of Civilization VII 1.0.0**.

## 1. SD card

Download **SD package** in the panel. Back up existing files before replacing them. Extract the ZIP contents into the SD root; the result must be `SD:/iz2s/...`, not an extra folder around `iz2s`.

If an older IZ2S is installed, update the SD files as part of this procedure. Merge folders and replace matching package files. Preserve `Nintendo`, other applications, BIOS, games, saves and settings. Keep the included licenses. Safely eject the card, then insert it into the powered-off console.

The package contains the matching native runtime, launcher resources, IZ2S Cheese, IZPSX, IZPS2 and Foldrift. It deliberately does not distribute a console-specific GPU cache. Do not combine the former R-Entry and Menu revisions or manually change hashes to silence a verification failure.

## 2. Prepare PC

Open the panel's PC preparation step. Supply your own `prod.keys` from your own Switch 1 through the panel; keys are not provided. Complete the dependency and permissions checks shown there. The panel provides the permission command when Linux networking requires it.

Choose the Wi-Fi radio used for LDN delivery. Ethernet alone cannot perform this step. The selected radio is temporarily released for delivery and restored afterwards. Keep the console on a network that the PC can reach after restoration. Do not use WSL for the initial LDN operation.

## 3. Connect and install save

1. Fully reboot the console.
2. Select **English** for the interface and **English (US)** for USB keyboard input, before launching Civilization VII:
   - Interface: `HOME → System Settings → System → Language → English`.
   - Input: `HOME → System Settings → Controllers & Accessories → USB Keyboard → English (US)`.
3. **If you have an older IZ2S version or previously used another loader:** follow the panel's conditional cleanup instructions. The menu path is `HOME → System Settings → Data Management → Delete Save Data → Civilization VII → Your profile`. This removes that profile's Civilization VII save data and game progress; it is separate from the normal trigger-save installation.
4. Create a fresh lobby: `Civilization VII 1.0.0 → Multiplayer → Wireless → Create Game → Host Lobby`.
5. Confirm **I rebooted the console and opened a new wireless lobby**, then press **Connect and install save**.

The panel delivers the initial loader, restores the PC's Wi-Fi, checks the loader and automatically installs `IZ2SCIV.Civ7Save`. Leave **Awaiting payload** open until completion. A second manual install action is not required on success. The installer writes its trigger through a temporary file and requires a confirmed filesystem commit; other ordinary game saves are preserved by this operation.

**Session status** shows progress and recovery actions. If automatic discovery fails, open **Console IP: automatic search or address selection**, directly below that status card. Find addresses, select and verify one, or enter the address shown on the console. Network candidates are not proof of a compatible loader; the panel validates it before sending. Return to automatic detection when needed.

If save installation fails after connection succeeds, use the offered retry action. If the network restoration fails, use **Restore PC Wi-Fi** before another transfer. After an interrupted LDN delivery, open a new lobby and confirm it before retrying. Session logs are available for diagnosis.

## 4. Launch IZ2S

Return to **HOME**, close Civilization VII with **X**, reopen Civilization VII, then choose **Load Game**. **Do not reboot the console at this point.**

Wait for **IZ2S CORE IS RUNNING**. The screen may turn black; allow about a minute before continuing as directed by the panel. Close Civilization VII normally, then hold **R** while opening a Switch 2 game. If the first attempt records the game's launch policy without opening IZ2S, repeat the R-held launch once.

If the console reports FAILED, filesystem access is unavailable, or the attempt freezes, fully reboot and retry the launch procedure. If it fails more than seven times, repeat the full installation procedure. A persistent `sd-verify` failure requires matching SD files and a newly installed matching trigger save; do not disable integrity checks.

For later launches, use the panel's **Launch** view. It offers the launch instructions and a single route to repeat the complete setup when necessary. When a console connection is already active, connection status and tools come first; manual launch help can be expanded if needed.

## PC Link and FTP

In IZ2S, **R + L3** toggles PC Link. **L + R3** toggles FTP. L3/R3 are stick clicks. Open the panel's connection card to see each connection independently.

For FTP, click **Connect FTP** in the panel after enabling the console's FTP server. Open **Super File Bros** for files or **App library** for applications. The normal console FTP port is 1425; use the actual address and port displayed on the console. After removing the console from its dock, connect it to Wi-Fi that can reach the PC.

## Updating an existing installation

Install the new PC archive into a new folder. Back up replaced SD files on the PC, copy only the prepared SD package and verify the copy before safe removal. Use a card reader or an independent FTP app if IZ2S has a runtime resource open; its own FTP server cannot replace a locked ROMFS safely.

Update SD files and repeat the complete panel procedure when the runtime/save compatibility changes. The generated launch save embeds the selected runtime's compatibility manifest. PC-only documentation or layout changes do not by themselves require a new console save.

Keep the console's working shader cache. Cache replacement is fixed in this revision, and validation remains tied to the shader and driver. A first launch that legitimately needs a new cache can still take longer than a warm launch. PC verification does not measure the complete R-to-menu startup time.
