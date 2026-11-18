# Catalog format

Save `catalog.tsv` as UTF-8, with one record per line, literal tabs between fields, and a final newline. The first line is `IZSTORE2`. There is no fixed application-count limit. Each app must have a unique lowercase ID. Catalog records and filtered indices use dynamically allocated memory.

| Record | Fields after the record name |
| --- | --- |
| `app` | ID, `iza` or `nro`, version, title, author, category, description |
| `repository` (optional) | HTTPS project URL |
| `file` | Exact size in bytes, lowercase SHA-256, `launcher.iza` or `launcher.nro` |
| `github` | Direct URL to the launcher in a tagged GitHub Release |

Keep the records for each app together in this order. A mixed IZA and NRO catalog requires Store 0.8.0 or later. The kaiHEN edition can read it and install NRO entries. Files may be up to 128 MiB each. Keep the same ID across updates.

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

Reserve `iz2s-cheese` for IZ2S Cheese itself. Use an official release URL of the form `https://github.com/byizmysh/iz2s-cheese/releases/download/v<VERSION>/iz2s-cheese.iza` or its `.nro` counterpart. A feed has unique app IDs, so publish a feed for each Store edition when including its self-update entry. IZA needs its companion `file` row; publish that exact `.izdata` asset in the same release. List sizes and SHA-256 from the actual release files, including same-version rebuilds. Updates are available from Settings; IZ2S Cheese cannot remove itself.
