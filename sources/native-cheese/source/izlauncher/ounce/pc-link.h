#pragma once
#include <stdbool.h>
#include <stdint.h>

#define IZ_PC_LINK_PACKAGE "sdmc:/iz2s/incoming/.pc-delivery.izm"

enum IzPcDelivery {
    IZ_PC_IDLE, IZ_PC_EMPTY, IZ_PC_PACKAGE, IZ_PC_ERROR, IZ_PC_FILES,
    IZ_PC_UPDATE_REQUIRED, IZ_PC_IMPORT_REPORTED, IZ_PC_TRANSFER_REPORTED
};
uint32_t iz_pc_start(void);
uint32_t iz_pc_read_key(uint8_t key[32]);
bool iz_pc_fetch(void);
#define IZ_PC_STORE_LIMIT 64u
typedef struct {char id[48];char name[64];} IzPcStoreApp;
bool iz_pc_store_request(const char *id);
bool iz_pc_store_result(IzPcStoreApp *apps,unsigned *count,unsigned *status,bool *selected);
bool iz_pc_receive_progress(uint32_t *received,uint32_t *total);
bool iz_pc_menu_idle(void);
void iz_pc_allow_auto_fetch(bool allowed);
bool iz_pc_online(void);
enum IzPcDelivery iz_pc_delivery(uint32_t *previous_import_code);
void iz_pc_import_result(uint32_t code);
struct IzNativeApp;
uint32_t iz_pc_video_open(uint32_t source,uint64_t *token);
uint32_t iz_pc_video_update(struct IzNativeApp *app,uint64_t token);
uint32_t iz_pc_video_close(struct IzNativeApp *app,uint64_t token);
void iz_pc_close(void);
