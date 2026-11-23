#ifndef IZ_STORE_UI_H
#define IZ_STORE_UI_H

#include "store.h"

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define ART_SIZE STORE_ICON_SIZE
#define ART_CACHE_SIZE 16
#define STORE_PAGE_SIZE 5
#define BRAND_SIZE 56
#define FONT_CELL_WIDTH 40
#define FONT_CELL_HEIGHT 44
#define FONT_GLYPHS 95
#define FONT_COUNT 4
#define SEARCH_BYTES 96

typedef struct {
    uint8_t advance[FONT_GLYPHS];
    uint8_t mask[FONT_GLYPHS][FONT_CELL_HEIGHT][FONT_CELL_WIDTH];
} Face;

typedef struct {
    Face fonts[FONT_COUNT];
    uint32_t brand[BRAND_SIZE][BRAND_SIZE];
    bool brand_loaded;
    uint32_t icons[ART_CACHE_SIZE][ART_SIZE * ART_SIZE];
    bool loaded[ART_CACHE_SIZE], used[ART_CACHE_SIZE];
    unsigned indices[ART_CACHE_SIZE];
} Artwork;

typedef enum { CATALOG, DETAILS, CONFIRM_REMOVE, SETTINGS, CONFIRM_COMPATIBILITY, CATALOG_SETTINGS, SETTINGS_RESULT, REPOSITORY, ABOUT } Screen;
typedef enum {
    NO_JOB, REFRESH, INSTALL, UPDATE_STORE, REMOVE, EXIT, FETCH_LIST, CHECK_NETWORK,
    STARTUP_CHECK, CANCEL_DOWNLOAD, LOAD_CATALOG, OPEN_CATALOGS, CHECK_STORE_UPDATE,
    ADD_CATALOG, EDIT_CATALOG, TOGGLE_CATALOG, REMOVE_CATALOG, ENABLE_COMMUNITY, EDIT_SEARCH
} Command;
enum {
    SETTINGS_CATALOGS, SETTINGS_DOWNLOAD, SETTINGS_NETWORK, SETTINGS_CHEESE_UPDATE,
    SETTINGS_ABOUT, SETTINGS_COUNT
};
typedef enum {
    BTN_UP = 1u, BTN_DOWN = 2u, BTN_LEFT = 4u, BTN_RIGHT = 8u,
    BTN_A = 16u, BTN_B = 32u, BTN_X = 64u, BTN_Y = 128u,
    BTN_L = 256u, BTN_R = 512u, BTN_PLUS = 1024u, BTN_MINUS = 2048u,
    BTN_ZL = 4096u, BTN_ZR = 8192u
} Button;

typedef struct {
    Screen screen;
    Screen catalogs_back;
    unsigned filter, cursor, count;
    unsigned settings_row;
    unsigned catalog_row;
    unsigned *visible;
    size_t visible_capacity;
    bool busy, failure, compact, inside_iz2s, cancellable, minimized;
    bool update_result;
    char search[SEARCH_BYTES];
    char source[RELEASE_URL_MAX];
    char download_title[64];
    unsigned percent;
    char message[STORE_MESSAGE];
    char result_title[64];
    char network[STORE_MESSAGE], addresses[STORE_MESSAGE];
    char github[STORE_MESSAGE];
    StoreSettings settings;
} View;

int artwork_slot(const Artwork *art, unsigned index);
bool artwork_open(Artwork *art, const char *folder);
bool artwork_read_icon(const char *folder, const App *app, uint32_t *pixels);
void artwork_update(Artwork *art, const char *folder, const Store *store);
void view_filter(View *view, const Store *store);
void view_search(View *view, const Store *store, const char *query);
Command view_input(View *view, const Store *store, unsigned buttons);
Command view_touch(View *view, const Store *store, int x, int y);
void view_draw(uint32_t *pixels, unsigned stride, const Artwork *art, const Store *store, const View *view);

#endif
