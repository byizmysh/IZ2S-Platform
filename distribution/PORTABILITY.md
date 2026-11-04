# Host portability

The [compatibility matrix](../COMPATIBILITY.md) is the status reference. The [manual test plan](HOST-TESTING.md) covers installation through session shutdown. Windows/macOS entry files now exist; their presence does not remove the remaining native-host port/qualification gate.

| Layer | Implementation |
| --- | --- |
| Networking | Standard Node.js TCP/HTTP/UDP and IPv4 interface inventory; route lookup sends no probe payload |
| Runtime preparation | Linux package-manager installer; Windows PowerShell/CMD and macOS command/shell bootstraps; shared managed-Python setup |
| Downloads | Official Node checksums, pinned uv/OpenSSH asset SHA-256; uv-managed Python and binary Pillow wheels |
| Python calls | Common resolver prefers private interpreter; supports host-specific executable paths and an explicit IZ2S_PYTHON override |
| Fonts | Bundled DejaVu family, manifest and license; optional override |
| Browser/folder integration | Optional OS adapters; --no-open works without an opener |
| ADV card copying | Positive automatic card identification on Linux; manual downloaded ZIP on Windows/macOS |
| ARM input device | Separate Linux USB-gadget board, not the server OS; input API compatibility is independent of launcher version |
| Optional native modules | Need their own host declarations/binaries; the PC installer cannot convert Linux executables |
| Native console host | Current descriptors remain Linux-only; graceful shutdown/process ownership, links, retained IPC and real hardware must be reviewed and tested before enabling Windows/macOS |

Private runtimes live in the version's data/runtime directory. Bootstrapped Node is also cached in the per-user bootstrap directory on Windows/macOS; no machine-wide PATH or PowerShell execution-policy change is performed. Node/Python sources and component licensing are linked in the FAQ and THIRD_PARTY.json.

Prepared Windows/macOS ZIP names contain **experimental**. They are local installation-test candidates. The installer can open the panel while its current host remains unsupported for native console sessions. Do not remove compatibility checks or mark an OS passed merely because installation succeeded.
