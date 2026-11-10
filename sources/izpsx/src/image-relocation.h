#ifndef PS1_IMAGE_RELOCATION_H
#define PS1_IMAGE_RELOCATION_H
#include <stddef.h>
#include <stdint.h>
/* Relocate only a private, freshly read native image before mapping/execution.
 * PS1RLC01 records were generated from the independently audited link snapshot.
 * Every record is checked before any image mutation. This validates relocation
 * structure, not authenticity; the bootstrap authenticates source/table/output
 * bytes before executing the relocated image. There are no SDK/global accesses.
 */
uint32_t ps1_relocate_image(uint8_t *code,size_t code_bytes,uint8_t *data,size_t data_bytes,
 const uint8_t *table,size_t table_bytes,uint64_t new_code,uint64_t new_data,
 uint64_t expected_old_code,uint64_t expected_old_data,uint64_t expected_data_region);
#endif
