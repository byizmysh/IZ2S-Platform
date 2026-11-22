#pragma once
#include <sb4/exchange.h>
#include <stdbool.h>

#define IZ_FTP_PORT 1425
uint32_t iz_ftp_enable(void);
bool iz_ftp_enabled(void);
bool iz_ftp_listening(void);
uint32_t iz_ftp_address(void);
void iz_ftp_close(void);
uint32_t iz_ftp_exchange(uint32_t command,Sb4ExchangeFtpState *state);
