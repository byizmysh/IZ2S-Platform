# Platform source packaging

This is the published IZPS2 0.5.0 source tree. Inherited `.gitattributes` files have an appended `* -text !eol` rule so Git preserves the original CRLF/LF bytes and the Platform integrity checks work after checkout. `source/SOURCE-MANIFEST.json` records this metadata-only adjustment. No emulator code or binary was changed. The original source archive URL and checksum are in `distribution/application-origins.json` at the Platform root.
