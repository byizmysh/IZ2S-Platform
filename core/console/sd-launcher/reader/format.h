#ifndef IZ_READER_FORMAT_H
#define IZ_READER_FORMAT_H
#include "native-app.h"
enum {READER_CONFIG=0x52425a49u,READER_STATE=0x53525a49u,READER_SAVE=0x53524249u};
#define READER_BOOKS 128u
#define READER_TEXT (8u*1024*1024)
#define READER_PAYLOAD (READER_TEXT+8192u)
#define READER_LINES 262144u
#define READER_DATA (16u*1024*1024)
#define READER_DYNAMIC 2097152u
#define READER_LIBRARY READER_DYNAMIC
#define READER_LIBRARY_META (READER_LIBRARY+128u*64u)
#define READER_DICTIONARY (READER_LIBRARY_META+128u*8u)
#define READER_DICTIONARY_META (READER_DICTIONARY+4096u)
#define READER_TITLE (READER_DICTIONARY_META+1024u)
#define READER_OUTPUT (READER_TITLE+64u)
#define READER_GPU (6u*1024*1024)
#define READER_NONE 0xffffffffu
typedef struct {uint32_t magic,version,bytes,state_offset,books,assets,initial_book,initial_open,prefs[7],reserved;} ReaderConfig;
typedef struct {char id[24],title[64];uint32_t asset,offset,bytes,text_at,text_bytes,crc,position,dictionary,reserved[2];} ReaderBook;
typedef struct {char path[80];uint32_t bytes,reserved;} ReaderAsset;
typedef struct {uint32_t offset,bytes,position;} ReaderLine;
_Static_assert(sizeof(ReaderConfig)==64&&sizeof(ReaderBook)==128&&sizeof(ReaderAsset)==88&&sizeof(ReaderLine)==12,"Reader data format");
#endif
