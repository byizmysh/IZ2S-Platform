#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const long spread_gib = strtol(argv[2], NULL, 10);
    if (!spread_gib || spread_gib < -128 || spread_gib > 128) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END)) return 2;
    const long length = ftell(file);
    if (length < (long)sizeof(IzRelocHeader) || fseek(file, 0, SEEK_SET)) return 2;
    unsigned char *bytes = malloc(length);
    if (!bytes || fread(bytes, 1, length, file) != (size_t)length || fclose(file)) return 2;
    const IzRelocHeader *header = (void *)bytes;
    void *sections[2] = {bytes, bytes};
    IzRelocState state = {0};
    int crc_matches = 0;
    uint32_t result = iz_reloc_begin(&state, header, length, sections, header->source_bases);
    if (result) goto done;
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t count = i < 2 ? header->file_bytes[i] : header->record_count * header->record_bytes;
        if (crc32(0, bytes + header->offsets[i], count) != header->crc32[i]) {
            result = 0xe110;
            goto done;
        }
    }
    crc_matches = 1;
    sections[0] = bytes + header->offsets[0];
    sections[1] = bytes + header->offsets[1];
    const IzRelocRecord *records = (void *)(bytes + header->offsets[2]);
    IzRelocReach reach;
    result = iz_reloc_reach_begin(&reach, header);
    for (uint32_t at = 0; !result && at < header->record_count;) {
        uint32_t count = header->record_count - at;
        if (count > IZ_RELOC_CHUNK_RECORDS) count = IZ_RELOC_CHUNK_RECORDS;
        result = iz_reloc_reach_batch(&reach, header, records + at, count);
        at += count;
    }
    /* Deliberately place the two segments beyond ADRP and PREL32 reach. */
    const uint64_t code_base = 0x2000000000ull;
    const uint64_t distance = (uint64_t)(spread_gib < 0 ? -spread_gib : spread_gib) << 30;
    const uint64_t bases[] = {code_base, spread_gib < 0 ? code_base - distance : code_base + distance};
    if (!result && reach.restricted) result = 0xe606;
    if (!result) result = iz_reloc_begin(&state, header, length, sections, bases);
    for (uint32_t at = 0; !result && at < header->record_count;) {
        uint32_t count = header->record_count - at;
        if (count > IZ_RELOC_CHUNK_RECORDS) count = IZ_RELOC_CHUNK_RECORDS;
        result = iz_reloc_batch(&state, records + at, count);
        at += count;
    }
    if (!result) result = iz_reloc_finish(&state);
done:
    printf("{\"result\":%u,\"crcMatched\":%s,\"records\":%u,\"memoryBytes\":[%u,%u],\"spreadGiB\":%ld}\n",
           result, crc_matches ? "true" : "false", state.processed,
           header->memory_bytes[0], header->memory_bytes[1], spread_gib);
    free(bytes);
    return result ? 1 : 0;
}
