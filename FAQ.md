# Quick help

- **How do I install?** Extract the complete OS-specific archive. Linux: `sh ./install.sh --launch`. Windows: double-click `Start.cmd` (`Install.cmd` is an alias).
- **How do I start it later?** Linux: use the desktop/application-menu shortcut or `start.sh`. Windows: keep the extracted folder and use `Start.vbs` or `Start.cmd`.
- **Why is a terminal visible on Windows?** `Start.cmd` shows first-run setup and diagnostics. Use `Start.vbs` for later launches without a console window.
- **How do I prepare Switch 1 on Windows?** Download the SD package in Device setup and extract it at the SD card root. Keep `switch/iz2skeyboard/iz2skeyboard.nro` with the generated pairing file.
- **Where do applications go?** Install through App library or export through the platform to `SD:/iz2s/modules/<id>/launcher.iza`, keeping the companion resources and signatures.
- **Where does Foldrift start?** The signed Foldrift package can open `SD:/` with the matching native filesystem service. Its folder is `SD:/iz2s/modules/foldrift/`.
- **Where do IZPSX BIOS and games go?** Follow [README.md](README.md#izpsx-bios-and-games). They are not included.
- **Can I stream from Windows?** The bundled BlitBun and VacooTube PC media services require Linux.
- **Which FTP port should I use?** Use the port shown in the console menu; the current default is **1425**.
- **Does Windows reinstall Node.js every time?** No. Node.js is bundled, and prepared dependencies are reused. A Windows Firewall prompt is a network permission request, not an installation request.
- **Do I need keys to connect Cardputer ADV or Switch 1?** You do not need to obtain or enter keys. Device setup generates pairing settings automatically. If the device was prepared on another PC, restore those settings or prepare it again. This is unrelated to Nintendo `prod.keys`.
- **Does Civilization VII work on Windows?** No. The tile is marked **Linux Only**; use Linux with a compatible Wi-Fi adapter for that launch route.
