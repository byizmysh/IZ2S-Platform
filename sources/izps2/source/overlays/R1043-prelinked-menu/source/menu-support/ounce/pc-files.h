#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define IZ_PC_FILE_LIMIT (256u*1024u)
#define IZ_PC_FILE_REQUEST_MAX 781u
uint32_t iz_pc_files(const uint8_t *request,size_t bytes,uint8_t *output,size_t *written);
void iz_pc_catalog_state(uint32_t page,uint32_t pages,uint32_t nro,uint32_t iza,uint32_t shown,uint32_t rejected,uint32_t error,uint32_t generation);
/* The PC link publishes an ID. Only the menu thread may claim it for removal. */
uint32_t iz_pc_request_remove(const uint8_t *id,size_t bytes);
bool iz_pc_take_remove(char id[48]);
void iz_pc_finish_remove(uint32_t result);
bool iz_pc_remove_result(uint32_t *result);
bool iz_pc_expire_remove(void);
