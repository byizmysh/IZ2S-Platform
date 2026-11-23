#pragma once
#include "../legacy/native/service-bridge.h"
const IzServiceBridge *iz_ounce_services(void);
/* Returns zero only after every bridge-owned socket/thread was released. */
uint32_t iz_ounce_services_close(void);
