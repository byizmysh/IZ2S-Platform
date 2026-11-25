# RUN32 Stability-1 native overlay

These are the exact four modified source files used by the packaged IZPS2 engine. They overlay the inherited R1324 link inputs: microVU sources from R1266, VMManager from R1304, and hosted POSIX from R1188. Engine SHA256: cb511c6a3465f95e6ee0047b4380ee7bef82e1abee671a86f3dc1fd6f9781dba.

Owner-thread writes alone did not fix the console crash. The release therefore keeps VU persistence disabled using the panel's shared SD-default policy. The diagnostic engine is retained byte-for-byte to avoid changing the hardware-tested configuration. The menu and its startup cache are separate components.

The native source tree retains the original licenses and inherited build instructions. The existing limitation on a clean full native rebuild remains; this overlay does not claim to supply every inherited toolchain/archive input. The supplied PC tests and console result describe the tested overlay, not unrestricted release qualification.
