#ifndef IZPSX_MEMORY_CARD_H
#define IZPSX_MEMORY_CARD_H
#include "fs-dir.h"
#define IZPSX_CARD_BYTES 131072u
/* Two shared PS1 cards. Each has two independently checked on-SD generations.
 * The last valid generation is never the write target. All I/O runs on Switch. */
uint32_t ps1_cards_open(Ps1SdDirectory *,void *card1,void *card2);
void ps1_card_mark(const void *data);
uint32_t ps1_cards_poll(uint64_t now,uint64_t hz,int force);
uint64_t ps1_cards_saved(void);
void ps1_cards_set_delay(unsigned milliseconds);
#endif
