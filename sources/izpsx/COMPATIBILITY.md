# Compatibility and verification boundary

Version: 0.5.1. Platform integration target: IZ2S Platform 0.22, platform API 1, compatible native launcher ABI 5. Compatibility is checked by the public ABI rather than hardcoding a launcher patch version. A future incompatible ABI still needs an explicit update; "all future versions" is not guaranteed.

The selected IZM declares Linux x64 and Linux ARM64 PC hosts. The payload itself runs AArch64 on the console. Windows and macOS builds are outside the 0.22 release scope.

RAM launch needs the local platform launcher and its matching executable/engine. The initialized SD route reads local game files. Shared file-manager access requires the platform's current verified FS endpoint and an idle native game boundary. No games/BIOS are supplied.

This local preparation verifies packaging, source fingerprints, ABI/container structure and JS tests. It does not claim a new physical-console test, audible audio qualification, every-controller coverage, long-session stability, universal game support or a performance improvement. Published releases must carry an accurate acceptance report for any additional claims.
