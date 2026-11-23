#include "ps2_sb4_preferences.h"

#include <string.h>

/* Like IZPSX, settings are written to alternating files. The header is
 * published last, so interruption leaves the other generation readable. */
enum { HEADER_BYTES = 64, SETTING_BYTES = HEADER_BYTES + sizeof(Ps2Preferences) };
static const char files[][20] = {"settings.a.izcfg", "settings.b.izcfg"};
static const unsigned char magic[8] = {'I','Z','P','S','2','C','F','G'};

static void put64(unsigned char *at, uint64_t value) {
    memcpy(at, &value, sizeof value);
}
static uint64_t get64(const unsigned char *at) {
    uint64_t value;
    memcpy(&value, at, sizeof value);
    return value;
}
static uint64_t checksum(const unsigned char *bytes, size_t length) {
    uint64_t result = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < length; ++i) {
        result ^= bytes[i];
        result *= UINT64_C(1099511628211);
    }
    return result;
}
static int valid_path(const char *path) {
    size_t size = 0;
    while (size < PS2_SB4_MEDIA_PATH_MAX && path[size]) ++size;
    if (size == PS2_SB4_MEDIA_PATH_MAX) return 0;
    if (!*path) return 1;
    int start = 1;
    for (; *path; ++path) {
        const unsigned char c = (unsigned char)*path;
        if (c < 32 || c == 127 || c == ':' || c == '\\' || (start && (c == '.' || c == '/')))
            return 0;
        start = c == '/';
    }
    return !start;
}
static int valid(const Ps2Preferences *p) {
    if (p->aspect > 3 || p->volume > 100 || p->full_boot > 1 ||
        p->multitap > PS2_MULTITAP_PORT_2 || !valid_path(p->last_game))
        return 0;
    for (unsigned i = 0; i < PS2_PAD_PLAYERS; ++i) {
        const Ps2PadProfile *profile = &p->profiles[i];
        if (p->devices[i] >= PS2_PAD_DEVICE_COUNT || profile->swap_ab > 1 ||
            profile->stick_dpad > 1 || profile->deadzone > 2 || profile->turbo_cross > 3) return 0;
    }
    return 1;
}
static uint64_t decode(const unsigned char bytes[SETTING_BYTES], Ps2Preferences *prefs) {
    if (memcmp(bytes, magic, sizeof magic) || get64(bytes + 8) != 1 ||
        get64(bytes + 24) != sizeof *prefs ||
        get64(bytes + 40) != checksum(bytes, 40) ||
        get64(bytes + 32) != checksum(bytes + HEADER_BYTES, sizeof *prefs))
        return 0;
    memcpy(prefs, bytes + HEADER_BYTES, sizeof *prefs);
    return valid(prefs) ? get64(bytes + 16) : 0;
}
static uint32_t read_all(const Sb4StorageFiles *storage, IzNativeApp *app,
                         uint32_t token, unsigned char bytes[SETTING_BYTES]) {
    uint32_t actual = 0;
    const uint32_t rc = storage->read(app, token, 0, bytes, SETTING_BYTES, &actual);
    return rc ? rc : actual == SETTING_BYTES ? 0 : PS2_SETTINGS_INVALID;
}

uint32_t ps2_settings_load(Ps2Settings *settings, IzNativeApp *app) {
    if (!settings) return PS2_SETTINGS_INVALID;
    memset(settings, 0, sizeof *settings);
    settings->prefs.volume = 100;
    for (unsigned i = 0; i < PS2_PAD_PLAYERS; ++i) settings->prefs.profiles[i].deadzone = 1;
    const Sb4StorageFiles *storage = sb4StorageFiles(sb4ModuleStorage(app));
    if (!storage) return PS2_SETTINGS_INVALID;

    for (unsigned slot = 0; slot < 2; ++slot) {
        uint32_t token = 0;
        uint64_t size = 0;
        uint32_t rc = storage->open(app, files[slot], SB4_STORAGE_OPEN_READ, &token, &size);
        if ((rc & 0x3fffffu) == PS2_SB4_LIBRARY_NOT_FOUND) continue;
        if (rc || !token) return rc ? rc : PS2_SETTINGS_INVALID;
        unsigned char bytes[SETTING_BYTES];
        if (size == SETTING_BYTES) rc = read_all(storage, app, token, bytes);
        const uint32_t closed = storage->close(app, token);
        if (rc) return rc;
        if (closed) return closed;

        Ps2Preferences prefs;
        const uint64_t generation = size == SETTING_BYTES ? decode(bytes, &prefs) : 0;
        if (!generation) settings->warning = PS2_SETTINGS_INVALID;
        else if (generation > settings->generation) {
            settings->prefs = prefs;
            settings->generation = generation;
            settings->active = slot;
        } else if (generation == settings->generation &&
                   memcmp(&settings->prefs, &prefs, sizeof prefs)) {
            return PS2_SETTINGS_INVALID;
        }
    }
    return 0;
}

uint32_t ps2_settings_save(Ps2Settings *settings, IzNativeApp *app) {
    if (!settings || !valid(&settings->prefs) || settings->generation == UINT64_MAX)
        return PS2_SETTINGS_INVALID;
    if (!settings->dirty) return 0;
    const Sb4StorageFiles *storage = sb4StorageFiles(sb4ModuleStorage(app));
    if (!storage) return PS2_SETTINGS_INVALID;
    const unsigned slot = settings->generation ? settings->active ^ 1u : 0;
    uint32_t token = 0;
    uint64_t size = 0;
    uint32_t rc = storage->open(app, files[slot], SB4_STORAGE_OPEN_UPDATE, &token, &size);
    if ((rc & 0x3fffffu) == PS2_SB4_LIBRARY_NOT_FOUND) {
        rc = storage->create(app, files[slot], SETTING_BYTES);
        if (!rc) rc = storage->open(app, files[slot], SB4_STORAGE_OPEN_UPDATE, &token, &size);
    }
    if (rc || !token) return rc ? rc : PS2_SETTINGS_INVALID;

    unsigned char bytes[SETTING_BYTES] = {0};
    if (size != SETTING_BYTES) rc = storage->resize(app, token, SETTING_BYTES);
    if (!rc) rc = storage->write(app, token, 0, bytes, HEADER_BYTES);
    if (!rc) rc = storage->flush(app, token);
    memcpy(bytes + HEADER_BYTES, &settings->prefs, sizeof settings->prefs);
    if (!rc) rc = storage->write(app, token, HEADER_BYTES, bytes + HEADER_BYTES,
                                  sizeof settings->prefs);
    if (!rc) rc = storage->flush(app, token);

    const uint64_t generation = settings->generation + 1;
    memcpy(bytes, magic, sizeof magic);
    put64(bytes + 8, 1);
    put64(bytes + 16, generation);
    put64(bytes + 24, sizeof settings->prefs);
    put64(bytes + 32, checksum(bytes + HEADER_BYTES, sizeof settings->prefs));
    put64(bytes + 40, checksum(bytes, 40));
    if (!rc) rc = storage->write(app, token, 0, bytes, HEADER_BYTES);
    if (!rc) rc = storage->flush(app, token);
    if (!rc) rc = read_all(storage, app, token, bytes);
    Ps2Preferences verified;
    if (!rc && (decode(bytes, &verified) != generation ||
                memcmp(&verified, &settings->prefs, sizeof verified)))
        rc = PS2_SETTINGS_INVALID;
    const uint32_t closed = storage->close(app, token);
    if (rc || closed) return rc ? rc : closed;
    rc = storage->commit(app);
    if (rc) return rc;

    settings->generation = generation;
    settings->active = slot;
    settings->dirty = 0;
    settings->warning = 0;
    return 0;
}
