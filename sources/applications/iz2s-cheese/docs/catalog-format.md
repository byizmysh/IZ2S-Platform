# Catalog format

Save `catalog.tsv` as UTF-8, with one record per line, literal tabs between fields, and a final newline. The new header is `IZSTORE3`; Cheese 0.8.2 also reads `IZSTORE1` and `IZSTORE2`. There is no fixed application-count limit. Catalog records and filtered indices use dynamically allocated memory.

With `IZSTORE3`, repeat the `app` group with the same ID once per build: one `iza` and one `nro`. Each build has its own version, `file` rows and `github` URL. Repeating the same ID and kind is invalid. Only one card is displayed: IZ2S prefers IZA when both exist, while kaiHEN selects NRO and hides IZA-only apps. Artwork remains one image per ID. The old `IZSTORE2` format continues to require unique IDs.

An online catalog can use a different TSV filename. To support Cheese 0.8.0–0.8.1 alongside 0.8.2 and later, publish the IZSTORE2 and IZSTORE3 feeds at separate URLs in the same folder so they can share artwork.

### Move an online catalog

Cheese 0.8.4 and later support a one-line redirect. Publish the new catalog first, then replace the old TSV with the following line. Use a literal tab between the marker and the absolute URL, followed by a newline.

```text
IZREDIRECT1	https://raw.githubusercontent.com/dadadush/switch2-homebrew-catalog/main/catalog/catalog-new2.tsv
```

Cheese checks enabled online catalogs at launch and on refresh. It follows up to four redirects on the same origin and saves the final URL after it downloads and validates the catalog. If the destination fails or the chain loops, Cheese keeps the saved URL and can use the previous cache. Older Cheese versions still need a feed at the old address.

| Record | Fields after the record name |
| --- | --- |
| `app` | ID, `iza` or `nro`, version, title, author, category, description |
| `repository` (optional) | HTTPS project URL |
| `file` | Exact size in bytes, lowercase SHA-256, `launcher.iza` or `launcher.nro` |
| `github` | Direct URL to the launcher in a tagged GitHub Release |

Keep the records for each build together in this order. Different IZA/NRO apps can share an `IZSTORE2` catalog on Cheese 0.8.0+. Two builds of the same app require `IZSTORE3` and Cheese 0.8.2+. Files may be up to 128 MiB each. Keep the same ID across updates.

### List an IZA release

Use an autonomous `.iza` for IZ2S. The catalog ID must match the ID inside the launcher. Publish the launcher and any companions in the same tagged GitHub Release. Use the names from the package’s `sd-bundle.json` for companion files.

Write one `file` row for `launcher.iza`, followed by one `file` row per `asset-<sha256>.izdata` companion. Put the `github` row last and point it to the `.iza` release asset. IZ2S Cheese downloads the companions by their exact names from the same release.

For an IZA with no companion files, the rows look like this. Replace the placeholders with the actual size and SHA-256, and use literal tab separators.

```text
app	my-app	iza	1.0.0	My App	Publisher	Tools	A Switch 2 homebrew app.
file	<bytes>	<sha256>	launcher.iza
github	https://github.com/OWNER/REPO/releases/download/v1.0.0/my-app.iza
```

### Update a catalog entry

To update an entry, edit the version, release URL, byte count, and SHA-256 in the same commit. For a local copy of the exact release asset, use `wc -c < app.nro` and `sha256sum app.nro`. New publisher releases become visible after the catalog is updated and users refresh it.

`artwork.bin` is optional. New packs use `IZART2\n` followed by an unsigned 32-bit little-endian image count. Each record contains a 48-byte NUL-padded app ID followed by 96 × 96 RGBA pixels in row order. Use the same IDs as in the TSV. Existing `IZART1\n` packs with their unsigned one-byte image count remain supported. Counts and file lengths are validated before reading images. Thumbnails are loaded on demand into a bounded cache; a larger catalog does not allocate every image in memory.

<details>
<summary>Offline IZA packages</summary>

For a separate SD-only catalog, use the `IZSTORE1` header and omit the
`repository` and `github` rows. In the `app` row, use `izm` for an IZA
package or `nro` for an NRO. For IZA, list `launcher.iza` first and then
any required `asset-<sha256>.izdata` companions as additional `file` rows.
The IZA package ID must match the catalog ID.

Place the files named by the `file` rows under
`/iz2s/iz2s-cheese/payloads/<id>/<launcher-sha256>/` on microSD.
Use the launcher's SHA-256 as the directory name. IZA packages are
supported by the IZ2S edition of IZ2S Cheese.

</details>

## Cheese updates

Reserve `iz2s-cheese` for IZ2S Cheese itself. Use an official release URL of the form `https://github.com/byizmysh/iz2s-cheese/releases/download/v<VERSION>/iz2s-cheese.iza` or its `.nro` counterpart. An `IZSTORE3` feed can include both groups under this ID. IZA needs its companion `file` row; publish that exact `.izdata` asset in the same release. Both groups share the `iz2s-cheese` artwork image. List sizes and SHA-256 from the actual release files, including same-version rebuilds. Cheese selects its own build and cannot remove itself. Settings can also check GitHub Releases directly, so self-update does not depend on this catalog entry.

## Dependency limits

Additional IZA `file` records are content-addressed companion assets installed beside `launcher.iza`. An NRO entry currently supports a single executable. The catalog is not a ZIP extractor or a general folder/package installer: it cannot recursively download a repository or place arbitrary dependency trees on SD. Keep external requirements explicit in the description rather than listing an incomplete package as a complete installation.
