# Host portability in 0.22

The [compatibility matrix](../COMPATIBILITY.md) defines this release's Linux x64/ARM64 scope. The [acceptance plan](HOST-TESTING.md) covers installation through session shutdown. Windows and macOS entry points, archives and installer workflows are not shipped.

| Area | Linux release behavior |
| --- | --- |
| Installation | apt, pacman or dnf prerequisites; verified private Node runtime when required; bundled fonts |
| Files and resources | Source manifest checks hashes, sizes and executable flags; private runtime data is excluded |
| Input | Cardputer ADV or a configured ARM Linux device; firmware flashing is an explicit separate action |
| Civ7 | First setup through Linux LDN or configured USB input; later Launch through LAN/Wi-Fi |
| Native console host | Existing component platform declarations and resource ownership remain enforced |

Shared source may retain cross-platform abstractions for future work. Their presence is not an installer, native-host port or qualification claim for another operating system. Physical Linux ARM64, console, controller and long-session checks must be recorded separately from archive and offline preview validation.
