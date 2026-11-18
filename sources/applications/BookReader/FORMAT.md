# Book Reader format 1

A `.leaf` file has a 44-byte header: ASCII `LFB1`, little-endian original byte length, compressed byte length and a 32-byte SHA-256 digest, followed by Brotli-compressed English text. Import and decode sizes are bounded. Compression is storage encoding, not encryption.

The GPU edition is a separate complete book layout. Its header points to the font atlas, font advances, up to 128 dictionary words, encoded lines, page indexes and library titles. Token bytes 0-127 are literal ASCII; 128-255 select a dictionary word of at most 25 characters. Each line decodes to at most 192 characters; a page has at most 64 lines. A compute invocation decodes only when the page changes. Font positions use fixed-point advances, and the fragment shader searches those positions before sampling the antialiased glyph atlas.

The font atlas starts at data byte 131072. Dynamic book bytes start at byte 2097152; font data is retained across layout changes. The total reader data budget is 6 MiB. Application scratch words carry page, menu, progress and a sequenced host mailbox. Book selection and typography requests are serviced at the platform's existing safe app boundary. A page-progress write does not upload the book again.

Changing typeface, weight, size, spacing, margins or orientation creates a fresh layout. The source-text position is carried into the new page index. Font settings and books live in private module data outside the IZM. Only console assets are forwarded to Switch.
