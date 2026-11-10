#ifndef IZ_RELOCATABLE_IMAGE_H
#define IZ_RELOCATABLE_IMAGE_H
#include <stddef.h>
#include <stdint.h>
/* A prelinked two-segment ARM64 image plus bounded runtime fixups. This is
 * an engine image, not an IZ native-app entry and not a launch permission. */
typedef struct {
 char magic[8];uint32_t version,header_bytes;uint64_t total_bytes,source_bases[2];
 uint32_t file_bytes[2],memory_bytes[2];uint64_t offsets[3];
 uint32_t record_count,record_bytes,crc32[3],entry_offset,spawn_offset,flags;
 uint8_t padding[144];
} IzRelocHeader;
enum {IZ_RELOC_ABS64=1,IZ_RELOC_PREL32,IZ_RELOC_PREL64,IZ_RELOC_ADRP,IZ_RELOC_MOVW};
#define IZ_RELOC_CHUNK_RECORDS 2048u
typedef struct {
 uint32_t offset;uint16_t kind;uint8_t segment,target_segment;
 uint64_t expected,target_offset,reserved;
} IzRelocRecord;
typedef struct {
 const IzRelocHeader *header;uint8_t *segments[2];uint64_t bases[2];
 uint32_t processed,last_segment,last_end,failed;
} IzRelocState;
_Static_assert(sizeof(IzRelocHeader)==256,"Relocatable header ABI");
_Static_assert(sizeof(IzRelocRecord)==32,"Relocatable record ABI");
/* The caller verifies each input section CRC while reading it and owns the
 * writable destinations. Never map executable pages or enter the image until
 * all records, CRCs and finish have succeeded. A failure fences this state. */
uint32_t iz_reloc_begin(IzRelocState*,const IzRelocHeader*,uint64_t,void*[2],const uint64_t[2]);
uint32_t iz_reloc_batch(IzRelocState*,const IzRelocRecord*,uint32_t);
uint32_t iz_reloc_finish(IzRelocState*);
#endif
