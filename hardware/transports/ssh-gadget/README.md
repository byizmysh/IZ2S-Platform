# ARM input protocol

The ARM board is an input device. Its installation has its own lifecycle and is
independent of the PC launcher version or engine selection.

IZ2S accepts `iz2s.input-agent`, ID `arm-input`, input API **1**, including the
unchanged 1.0.0 package shipped with RUN20. It checks installed executable files
against the ARM installation's own SHA-256 inventory. It never compares their
hashes to the PC's package to decide compatibility.

Connection checks, launch, engine selection and launcher updates never install
or replace ARM files. Only the explicit First-time setup installer deploys the
PC package. That package retains its own checksum validation before deployment.

API 1 uses `status`, `enqueue`, `follow` and `halt` with engine-provided typed
plans. Keep this contract, job identifiers and `/run/ns2jb-keyboard-jobs` stable
across launcher releases. Incompatible changes require a new input API and an
explicit compatibility decision in the PC transport. Cancelling/following an
existing job remains available even when the package metadata needs repair.

Discovery is public LAN metadata only; SSH authenticates input commands. A
discovery-service update is not required merely because a launcher changes.
