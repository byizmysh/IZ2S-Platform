#!/usr/bin/env python3
"""make_payload.py - stage a payload JS for the chat-typed transport.

Usage: python tools/make_payload.py <payload.js>
Writes ../core.js = the payload verbatim (CRLF-normalized). No wrapper is
added: the chat eval context is synchronous, so the payload must not use
top-level await - if yours does, wrap it in its own
`(async function(){ ... })();` first (node --check core.js must pass).

Then run: npm install && node compress.js && cmake --build build
"""
import io, sys

src_path = sys.argv[1] if len(sys.argv) > 1 else "payload.js"
core = io.open(src_path, encoding="utf-8").read().replace("\r\n", "\n")
io.open("../core.js", "w", encoding="utf-8", newline="\n").write(core)
print("../core.js written:", len(core), "bytes (from", src_path + ", verbatim)")
