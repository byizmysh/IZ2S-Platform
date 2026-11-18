# Quick help

- **How do I install?** Extract the complete OS-specific archive. Linux: `sh ./install.sh --launch`. Windows: double-click `Install.cmd`.
- **How do I start it later?** Use the desktop/application-menu shortcut with the IZ2S icon. Linux also has `start.sh`; Windows has `Start.vbs`.
- **Why is a terminal visible on Windows?** `Start.cmd` is the diagnostic entry. Use the shortcut or `Start.vbs` for normal startup.
- **How do I prepare Switch 1 on Windows?** Download the SD package in Device setup and extract it at the SD card root. Keep `switch/iz2skeyboard/iz2skeyboard.nro` with the generated pairing file.
- **Where do applications go?** Install through App library or export through the platform to `SD:/iz2s/modules/<id>/launcher.iza`, keeping the companion resources and signatures.
- **Where does Foldrift start?** The signed Foldrift package can open `SD:/` with the matching native filesystem service. Its folder is `SD:/iz2s/modules/foldrift/`.
- **Where do IZPSX BIOS and games go?** Follow [README.md](README.md#izpsx-bios-and-games). They are not included.
- **Can I stream from Windows?** The bundled BlitBun and VacooTube PC media services require Linux.
- **Which FTP port should I use?** Use the port shown in the console menu; the current default is **1425**.
