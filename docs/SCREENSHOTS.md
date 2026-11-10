# Screenshots for IZ2S Platform 0.22

These images were regenerated for the local 0.22 release candidate on 10 November 2026. The old 0.21 README images were replaced.

## PC panel

`images/connection.png` and `images/app-library.png` are browser captures of the actual current panel UI in English at a 1600 × 1080 viewport. The isolated documentation server used example local addresses, an example running session and four bundled modules shown on SD. It made no console connection and sent no input or native RPC command. The examples are interface illustrations, not a hardware qualification record. No account, pairing token, keys, game files or personal session history is shown.

## Console menus

`images/izlauncher.png` and `images/izpslauncher.png` are 1920 × 1080 offscreen renders of the shipped vertex/fragment shaders, font resources and bundled module artwork. The IZlauncher image uses the current purple gradient design, mouse header, RAM labels and recent activity; it is not the older launcher or an IZ2Launcher concept image. The shader binaries matched the current development launcher at capture time.

Run `node tools/docs-launcher-previews.cjs` on a Linux development host with the documented C/Vulkan/Pillow prerequisites to regenerate the console-menu images. Temporary inputs are written to ignored `data/build-cache/docs-0.22/`. Example install activity is synthetic and separate from real job history. These images do not capture a physical console or establish performance/animation frame-rate results.

## File fingerprints

| Image | SHA-256 |
| --- | --- |
| connection.png | `a1c39d80ec46b75687886e86893389c59725ca8135ca5e4ae713ef44d1241887` |
| app-library.png | `56017e8d4bac07313677fceefe88cf8a99a304ed20260f37e4263edb5dd75762` |
| izlauncher.png | `dbee2e6c6a78f03ba7f34df2f41799e3a7b462e7419cb0079accf718ba0bb58c` |
| izpslauncher.png | `b1f9b7f34da62a2f61154589221991c3d8d118949f6323e1c4b49321dd797a9c` |

| Shipped fragment shader | SHA-256 |
| --- | --- |
| izlauncher/app/app.frag.spv | `5a5c2b79eea7a4880cd2bdff5575069c1a9c05bae798b0b68f713fa5acb9a681` |
| izpslauncher/app/app.frag.spv | `76650c771bd8b12df951ff4dc38bffc904d593266f5067a427dd20c83a62b742` |
