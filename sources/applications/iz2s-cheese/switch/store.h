#ifndef IZ_HOMEBREW_STORE_H
#define IZ_HOMEBREW_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "download.h"


#define STORE_MAX_FILES 17
#define STORE_PATH 512
#define STORE_MESSAGE 160
#define STORE_ICON_SIZE 96
#define STORE_DIR "/iz2s/iz2s-cheese"
#define MODULE_DIR "/iz2s/modules"
#ifdef IZ_STORE_STANDALONE
#define STORE_CATALOG_DIR "/switch/iz2s-cheese/catalog"
#else
#define STORE_CATALOG_DIR MODULE_DIR "/iz2s-cheese/catalog"
#endif


typedef enum { PACKAGE_IZM, PACKAGE_NRO, PACKAGE_IZM_NRO } PackageKind;
typedef enum { NOT_INSTALLED, INSTALLED, UPDATE_AVAILABLE, EXTERNAL, UNAVAILABLE, SELF_ENTRY } InstallState;

typedef struct {
    char name[80];
    char digest[65];
    uint64_t bytes;
} PackageFile;

typedef struct {
    char id[48], title[64], author[60], version[24], category[24];
    char description[480];
    char release_url[RELEASE_URL_MAX];
    char repository[RELEASE_URL_MAX];
    PackageKind kind;
    unsigned count;
    uint32_t owned;
    PackageFile files[STORE_MAX_FILES];
} Package;

typedef struct {
    Package package;
    InstallState state;
    char installed_version[24];
    int icon;
} App;

typedef struct {
    char root[STORE_PATH];
    unsigned count, remote_count;
    App *apps;
} Store;

typedef struct { char url[RELEASE_URL_MAX]; bool online; } StoreSettings;

typedef void (*StoreProgress)(void *, uint64_t done, uint64_t total);
typedef struct { StoreProgress report; void *context; const atomic_bool *cancel; } Progress;

void store_clear(Store *store);
bool store_clone(Store *target, const Store *source);
bool store_load(Store *store, const char *root, char message[STORE_MESSAGE]);
bool store_refresh_catalog(Store *store, const char *root, const StoreSettings *settings,
                           char message[STORE_MESSAGE]);
void store_settings_read(const char *root, StoreSettings *settings);
bool store_settings_write(const char *root, const StoreSettings *settings, char message[STORE_MESSAGE]);
bool store_fetch_list(Store *store, const StoreSettings *settings, char message[STORE_MESSAGE]);
bool store_read_artwork_icon(const Store *store, const StoreSettings *settings,
                             unsigned slot, uint32_t *pixels);
unsigned store_fetch_artwork(const Store *store, const StoreSettings *settings);
unsigned store_read_artwork(const Store *store, const StoreSettings *settings,
                            uint32_t pixels[][STORE_ICON_SIZE * STORE_ICON_SIZE]);
#ifdef IZ_STORE_STANDALONE
void store_set_running_nro(const char *path, const char *version);
#else
void store_set_running_iza(const char *version);
#endif
const App *store_self_entry(const Store *store);
void store_refresh(const Store *store, App *app);
void store_sync_icon(const Store *store, const App *app, const char *artwork);
void store_sync_icon_pixels(const Store *store, const App *app, const void *rgba);
bool store_read_installed_icon(const Store *store, const App *app, uint32_t *pixels);
bool store_install(const Store *store, const App *app, Progress progress, char message[STORE_MESSAGE]);
bool store_remove(const Store *store, const App *app, char message[STORE_MESSAGE]);
const char *store_extension(PackageKind kind);
void store_repository(const Package *package, char url[RELEASE_URL_MAX]);

#endif
