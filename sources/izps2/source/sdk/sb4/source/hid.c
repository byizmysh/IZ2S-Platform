#include <switch.h>
#include <sb4/services.h>
#include "hid.h"

#define BAD MAKERESULT(Module_Libnx, LibnxError_BadInput)

static const HidNpadIdType launcher_ids[] = {
    HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
    HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8,
    HidNpadIdType_Handheld,
};
static Mutex mutex;
static bool launcher_ready;
static bool ids_changed;
static bool styles_changed;
static uint32_t original_styles;

static uint32_t set_launcher_ids(void) {
    return hidSetSupportedNpadIdType(launcher_ids, sizeof launcher_ids / sizeof *launcher_ids);
}

uint32_t sb4HidConfigureLauncher(uint32_t styles) {
    mutexLock(&mutex);
    if (launcher_ready) {
        mutexUnlock(&mutex);
        return BAD;
    }
    hidInitializeNpad();
    uint32_t rc = set_launcher_ids();
    if (!rc) rc = hidSetSupportedNpadStyleSet(styles);
    launcher_ready = rc == 0;
    mutexUnlock(&mutex);
    return rc;
}

uint64_t sb4HidGetSupportedStyles(SB4_SERVICE_ARGS) {
    uint32_t *out = (void *)(uintptr_t)a;
    if (!out) return BAD;
    mutexLock(&mutex);
    *out = 0;
    uint32_t rc = launcher_ready ? hidGetSupportedNpadStyleSet(out) : BAD;
    mutexUnlock(&mutex);
    return rc;
}

uint64_t sb4HidSetSupportedStyles(SB4_SERVICE_ARGS) {
    if (a > UINT32_MAX) return BAD;
    mutexLock(&mutex);
    uint32_t rc = launcher_ready ? 0 : BAD;
    if (!rc && !styles_changed) rc = hidGetSupportedNpadStyleSet(&original_styles);
    if (!rc) {
        styles_changed = true;
        rc = hidSetSupportedNpadStyleSet((uint32_t)a);
    }
    mutexUnlock(&mutex);
    return rc;
}

uint64_t sb4HidSetSupportedIds(SB4_SERVICE_ARGS) {
    const uint32_t *ids = (const void *)(uintptr_t)a;
    HidNpadIdType slots[10];
    if (!ids || !b || b > sizeof slots / sizeof *slots) return BAD;
    uint64_t seen = 0;
    for (size_t i = 0; i < b; ++i) {
        uint32_t id = ids[i];
        if (id > HidNpadIdType_No8 && id != HidNpadIdType_Other &&
            id != HidNpadIdType_Handheld) return BAD;
        uint64_t bit = UINT64_C(1) << id;
        if (seen & bit) return BAD;
        seen |= bit;
        slots[i] = (HidNpadIdType)id;
    }
    mutexLock(&mutex);
    uint32_t rc = launcher_ready ? 0 : BAD;
    if (!rc) {
        ids_changed = true;
        rc = hidSetSupportedNpadIdType(slots, b);
    }
    mutexUnlock(&mutex);
    return rc;
}

uint64_t sb4HidDisconnectPad(SB4_SERVICE_ARGS) {
    const uint32_t *slot = (const void *)(uintptr_t)a;
    if (!slot || *slot > HidNpadIdType_No8) return BAD;
    HidNpadIdType id = (HidNpadIdType)*slot;
    mutexLock(&mutex);
    uint32_t rc = launcher_ready ? hidDisconnectNpad(id) : BAD;
    mutexUnlock(&mutex);
    return rc;
}

uint32_t sb4HidRestoreLauncher(void) {
    mutexLock(&mutex);
    uint32_t rc = 0;
    if (ids_changed) {
        rc = set_launcher_ids();
        if (!rc) ids_changed = false;
    }
    if (styles_changed) {
        uint32_t style_rc = hidSetSupportedNpadStyleSet(original_styles);
        uint32_t restored = 0;
        if (!style_rc) style_rc = hidGetSupportedNpadStyleSet(&restored);
        if (!style_rc && restored != original_styles) style_rc = BAD;
        if (!style_rc) styles_changed = false;
        if (!rc) rc = style_rc;
    }
    mutexUnlock(&mutex);
    return rc;
}
