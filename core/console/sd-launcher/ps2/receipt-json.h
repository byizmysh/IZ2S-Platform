#ifndef PS2_RESIDENT_JSON_PRIVATE_H
#define PS2_RESIDENT_JSON_PRIVATE_H
#include <stdint.h>
#include <stddef.h>
enum{RJ_OBJECT=1,RJ_ARRAY,RJ_STRING,RJ_NUMBER,RJ_TRUE,RJ_FALSE,RJ_NULL};
typedef struct{uint32_t start,end;uint16_t child,next;uint8_t kind,pad[3];} RjNode;
typedef struct{const unsigned char*text;size_t bytes,at;uint16_t count;RjNode nodes[512];} Rj;
#define RJ_NONE 65535u
int rj_parse(Rj*,const unsigned char*,size_t);
uint16_t rj_get(const Rj*,uint16_t,const char*);
int rj_u64(const Rj*,uint16_t,uint64_t*);
int rj_is(const Rj*,uint16_t,unsigned);
#endif
