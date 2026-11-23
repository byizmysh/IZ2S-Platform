# Building

R1322 uses the IZ2S RUN25 package layout and the selected RUN23 Switch 2 SDK. The archive contains the engine and platform source trees, active overlays, and the recorded compile and link recipes under `source/recipes/`. Path placeholders identify the local development workspace, SDK and dependencies.

In the original development workspace run `python3 build-engine.py` from the R1322 candidate directory. It compiles the changed engine objects and the runner, relinks the native image, checks relocations, and produces the RUN25 IZA.

The release staging script accepts the RUN25 project folder, the R1322 candidate directory and the prior R1249 release directory. It reuses the saved launcher artwork and graphics resources, preserves the R1322 engine bytes, and verifies the branded IZA through offline import and SD ZIP readback.

A complete clean source rebuild is pending. The recorded recipes and input hashes identify the local toolchain and prebuilt dependencies used for the native build.
