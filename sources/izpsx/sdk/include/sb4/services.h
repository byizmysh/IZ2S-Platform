#ifndef IZ_SERVICE_BRIDGE_H
#define IZ_SERVICE_BRIDGE_H
#include <stdint.h>
#include <stddef.h>

/* These IDs retain the qualified SB4 profile's offsets as lookup keys only.
 * They are never added to an invented SDK base. A libounce target is a real
 * compiled adapter with the old callback's argument and output contract.
 * The bridge belongs to the launcher and outlives all application workers. */
typedef uint64_t (*IzServiceFunction)(uint64_t,uint64_t,uint64_t,uint64_t,
                                    uint64_t,uint64_t,uint64_t,uint64_t);
typedef struct {
    uint32_t id, reserved;
    const char *name;
    IzServiceFunction function;
} IzServiceBinding;
typedef struct {
    uint32_t version, bytes, count, provider;
    const IzServiceBinding *bindings;
} IzServiceBridge;
enum { IZ_SERVICE_LIBOUNCE = 2 };
enum {
    SB4_HID_SET_SUPPORTED_STYLES = 4795760,
    SB4_HID_GET_SUPPORTED_STYLES = 4795872,
    SB4_HID_SET_SUPPORTED_IDS = 4795984,
    SB4_HID_DISCONNECT_PAD = 4797392,
    SB4_HID_READ_FULL_KEY = 4845184,
    SB4_HID_READ_HANDHELD = 4845408,
    SB4_HID_READ_JOY_DUAL = 4847136,
    SB4_HID_READ_JOY_LEFT = 4847360,
    SB4_HID_READ_JOY_RIGHT = 4847584,
};
static inline IzServiceFunction iz_service_target(const IzServiceBridge *bridge, uint32_t id) {
    if (!bridge || bridge->version != 1 || bridge->bytes != sizeof(*bridge) ||
        bridge->provider != IZ_SERVICE_LIBOUNCE || bridge->count > 128 || !bridge->bindings)
        return NULL;
    for (uint32_t i = 0; i < bridge->count; ++i)
        if (bridge->bindings[i].id == id && !bridge->bindings[i].reserved)
            return bridge->bindings[i].function;
    return NULL;
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
const IzServiceBridge *sb4GetServiceBridge(void);
uint32_t sb4CloseServices(void);
/* Supervisor entry points. Configure establishes the menu's slots and styles.
 * Restore is called after the application and its workers have stopped. */
uint32_t sb4HidConfigureLauncher(uint32_t styles);
uint32_t sb4HidRestoreLauncher(void);
void sb4SetPadFocus(uint32_t active);
#ifdef __cplusplus
}
#endif
