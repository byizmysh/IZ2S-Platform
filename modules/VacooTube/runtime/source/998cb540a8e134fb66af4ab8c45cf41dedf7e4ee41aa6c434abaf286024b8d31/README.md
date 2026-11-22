# VacooTube application source

This is the player application contained in the VacooTube module. The root [README](../README.md) gives installation, source attribution and the complete Switch 2 / IZ2S workflow. Original copyright and MIT terms remain in [LICENSE](LICENSE).

`src/` owns the Electron main process, preload code, settings and TV-page integration. `locale/` owns translated strings; managed controls default to English. `assets/` contains VacooTube icons. `build/` and `flatpak/` retain build-reference metadata; these do not establish tested platform streaming support.

For the supported offscreen console workflow, install the module/runtime through the root instructions. The runtime prepares a separate digest-named copy with production npm dependencies; do not develop inside that cache. Edits here require source re-preparation and repacking the module before a fresh console session.

Desktop metadata can be built with the package scripts after installing the appropriate development tools, but Windows/macOS console audio paths are not supplied by this module. Automatic in-app updates are disabled in favor of IZ2S module releases. No source dependency preparation runs npm lifecycle scripts.
