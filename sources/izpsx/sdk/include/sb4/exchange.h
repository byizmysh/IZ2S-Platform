#ifndef SB4_EXCHANGE_H
#define SB4_EXCHANGE_H

#include <stdint.h>

struct IzNativeApp;

enum { SB4_EXCHANGE_STATUS, SB4_EXCHANGE_START, SB4_EXCHANGE_STOP };
typedef struct {
    uint32_t running,transferring,clients,port;
    uint64_t sent,received;
} Sb4ExchangeFtpState;

/* Optional Izlauncher import sb4FileExchangeFtp. The File Exchange module
 * owns a paired, SD-scoped listener until STOP. */
typedef uint32_t (*Sb4FileExchangeFtp)(struct IzNativeApp *,uint32_t command,Sb4ExchangeFtpState *);

#endif
