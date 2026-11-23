# R1324 ROMFS overlay

This overlay uses R1322 for the emulator core and launcher. It replaces the
R1189 storage provider with a version that serves the application resources
from a ROMFS in the engine's read-only segment.

Extract the `resources/` folder from `izps2-emulator-0.5.zip`, then run
`python3 pack-resource-romfs.py resources resources.romfs` here. Compile the
storage provider from this directory so the assembler can find
`resources.romfs`. Use the R1189 storage compiler flags for the provider and
`ps2_resource_romfs.c`. In the R1322 link recipe, replace its storage
provider object and add the ROMFS object as a direct link input. Run the
recorded image sealing, export, and relocation verification steps.

The package script consumes the ABI 7 launcher from the R1322 portable IZA
under the name `reference-launcher.iza`, the exported engine receipt
`engine.izdata.json`, and the ROMFS files. Set `IZ2S_ROOT` to the local
IZ2S RUN25 source checkout and pass the relocated engine image as the script
argument. The offline importer runs as part of packaging.
