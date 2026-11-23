#include "ui.h"
#include "github-mark.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { FILTER_ALL, FILTER_INSTALLED, FILTER_UPDATES, FILTER_TOOLS,
    FILTER_MEDIA, FILTER_EMULATORS, FILTER_GAMES, FILTERS };
enum { PAGE_SIZE = STORE_PAGE_SIZE, CARD_X = 272, CARD_Y = 142, CARD_H = 302, CARD_GAP = 14 };
enum { PAGE_Y = 585, PAGE_PREV_X = 1024, PAGE_NEXT_X = 1182, PAGE_BUTTON_W = 52, PAGE_BUTTON_H = 42 };
enum { RAIL_FULL = 246, RAIL_COMPACT = 82 };
enum { RAIL_ROW_Y = 114, RAIL_ROW_STEP = 53, RAIL_ROW_H = 43 };
enum {
    HEADER_ACTION_Y = 40, SEARCH_X = 832, SEARCH_W = 192, CATALOGS_X = 1038, CATALOGS_W = 196,
    WELCOME_ACTION_Y = 270, WELCOME_INSET = 24, WELCOME_PRIMARY_W = 336,
    WELCOME_GAP = 18, WELCOME_CATALOGS_W = 192
};
enum { SMALL, NORMAL, MEDIUM, LARGE };

static const char *const filters[FILTERS] = {
    "All apps", "Installed", "Updates", "Tools", "Media", "Emulators", "Games"
};
static const unsigned font_height[] = { 24, 28, 34, 42 };
static const uint32_t background = 0xff321832, panel = 0xff512f37, edge = 0xff815e71,
    bar = 0xff2c1931, sidebar = 0xff42294b, accent = 0xfff8e5a3,
    ink = 0xfffcf7fa, muted = 0xffe4d1d9, dim = 0xffc7afbf,
    success = 0xff9aedaa, warning = 0xff8bd5ff, nro_ink = 0xff93deff;

typedef struct {
    uint32_t *pixels;
    unsigned stride;
    const Artwork *art;
} Canvas;

static void rectangle(Canvas *canvas, int x, int y, int w, int h, uint32_t color) {
    int right = x + w, bottom = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > SCREEN_WIDTH) right = SCREEN_WIDTH;
    if (bottom > SCREEN_HEIGHT) bottom = SCREEN_HEIGHT;
    for (int row = y; row < bottom; ++row)
        for (int col = x; col < right; ++col) canvas->pixels[row * canvas->stride + col] = color;
}

static void outline(Canvas *canvas, int x, int y, int w, int h, int line_width, uint32_t color) {
    rectangle(canvas,x,y,w,line_width,color);
    rectangle(canvas,x,y+h-line_width,w,line_width,color);
    rectangle(canvas,x,y,line_width,h,color);
    rectangle(canvas,x+w-line_width,y,line_width,h,color);
}

static uint32_t blend(uint32_t under, uint32_t over, unsigned alpha) {
    unsigned inverse = 255 - alpha;
    unsigned r = ((under & 255) * inverse + (over & 255) * alpha) / 255;
    unsigned g = (((under >> 8) & 255) * inverse + ((over >> 8) & 255) * alpha) / 255;
    unsigned b = (((under >> 16) & 255) * inverse + ((over >> 16) & 255) * alpha) / 255;
    return 0xff000000u | r | (g << 8) | (b << 16);
}

#ifdef IZ_STORE_MODULE
// The fragment shader uses the otherwise unused alpha byte to identify text.
static uint32_t mark_text(uint32_t color) { return color & 0xfeffffffu; }
#else
static uint32_t mark_text(uint32_t color) { return color; }
#endif

static void rounded(Canvas *canvas, int x, int y, int w, int h, int radius,
                    uint32_t top, uint32_t bottom) {
    const int center = radius - 1;
    int cuts[24] = {0};
    for (int row = 0; row < radius; ++row) {
        int dx = center, dy = center - row;
        while (dx * dx + dy * dy > radius * radius) --dx;
        cuts[row] = center - dx;
    }
    for (int row = 0; row < h; ++row) {
        int mirror = row < h / 2 ? row : h - 1 - row;
        int cut = mirror < radius ? cuts[mirror] : 0;
        rectangle(canvas,x+cut,y+row,w-2*cut,1,blend(top,bottom,(unsigned)(255*row/h)));
    }
}

static int rail_width(const View *view, const Store *store) {
    return view->screen == CATALOG && store->count && view->compact ? RAIL_COMPACT : RAIL_FULL;
}
static int catalog_x(int rail) { return rail + 30; }
static int card_width(int left) { return (1234-left-(PAGE_SIZE-1)*CARD_GAP)/PAGE_SIZE; }

static unsigned glyph_index(unsigned char ch) { return ch >= 32 && ch <= 126 ? ch - 32 : '?' - 32; }

static int text_width(const Canvas *canvas, int size, const char *text) {
    int width = 0;
    for (const unsigned char *in = (const unsigned char *)text; *in; ++in)
        if ((*in & 0xc0) != 0x80) width += canvas->art->fonts[size].advance[glyph_index(*in)];
    return width;
}

static int text_ink_width(const Canvas *canvas, int size, const char *text) {
    int width = text_width(canvas, size, text);
    if (!width) return 0;
    const Face *font = &canvas->art->fonts[size];
    unsigned glyph = glyph_index((unsigned char)text[strlen(text) - 1]);
    int advance = font->advance[glyph];
    int last = advance + 1 < FONT_CELL_WIDTH ? advance + 1 : FONT_CELL_WIDTH - 1;
    for (int col = last; col >= 0; --col)
        for (unsigned row = 0; row < font_height[size]; ++row)
            if (font->mask[glyph][row][col]) return width - advance + col + 1;
    return width;
}

static void label(Canvas *canvas, int x, int y, int max, int size, uint32_t color, const char *value) {
    const Face *font = &canvas->art->fonts[size];
    int right = x + max;
    for (const unsigned char *in = (const unsigned char *)value; *in; ++in) {
        if ((*in & 0xc0) == 0x80) continue;
        unsigned ch = glyph_index(*in), advance = font->advance[ch];
        if (x + (int)advance > right) break;
        for (unsigned row = 0; row < font_height[size] && y + (int)row < SCREEN_HEIGHT; ++row) {
            if (y + (int)row < 0) continue;
            for (unsigned col = 0; col < advance + 2 && col < FONT_CELL_WIDTH; ++col) {
                int at = x + (int)col;
                unsigned alpha = font->mask[ch][row][col];
                if (!alpha || at < 0 || at >= right || at >= SCREEN_WIDTH) continue;
                uint32_t *pixel = &canvas->pixels[(y + row) * canvas->stride + at];
                *pixel = mark_text(alpha == 255 ? color : blend(*pixel, color, alpha));
            }
        }
        x += (int)advance;
    }
}

static void wrap(Canvas *canvas, int x, int y, int width, int lines, int size, uint32_t color, const char *value) {
    const Face *font = &canvas->art->fonts[size];
    while (*value && lines--) {
        int length = 0, last_space = 0, used = 0;
        while (value[length] && used + font->advance[glyph_index((unsigned char)value[length])] <= width) {
            if (value[length] == ' ') last_space = length;
            used += font->advance[glyph_index((unsigned char)value[length++])];
        }
        if (value[length] && last_space) length = last_space;
        if (!length) break;
        char row[480];
        memcpy(row, value, length);
        row[length] = '\0';
        label(canvas,x,y,width,size,color,row);
        value += length;
        while (*value == ' ') ++value;
        y += (int)font_height[size] + 1;
    }
}

static void cover(Canvas *canvas, int x, int y, int index, int size) {
    rectangle(canvas,x,y,size,size,0xff66534f);
    index = index < 0 ? -1 : artwork_slot(canvas->art, (unsigned)index);
    if (index < 0 || !canvas->art->loaded[index]) {
        outline(canvas,x+size/4,y+size/4,size/2,size/2,3,muted);
        return;
    }
    const uint32_t *pixels = canvas->art->icons[index];
    for (int row = 0; row < size; ++row)
        for (int col = 0; col < size; ++col) {
            int source_row = (2 * row + 1) * ART_SIZE / (2 * size);
            int source_col = (2 * col + 1) * ART_SIZE / (2 * size);
            uint32_t color = pixels[source_row * ART_SIZE + source_col];
            uint32_t *target = &canvas->pixels[(y+row)*canvas->stride+x+col];
            *target = color >> 24 == 255 ? color : blend(*target, color, color >> 24);
        }
}

static bool check_mark(InstallState state) { return state == INSTALLED || state == UPDATE_AVAILABLE || state == EXTERNAL; }

static void check(Canvas *canvas, int x, int y) {
    for (int i = 0; i < 7; ++i) rectangle(canvas,x+i,y+6+i,4,4,success);
    for (int i = 0; i < 14; ++i) rectangle(canvas,x+6+i,y+12-i,4,4,success);
}

static const char *state_name(const App *app) {
#ifdef IZ_STORE_STANDALONE
    if (app->package.kind == PACKAGE_IZM) return "Requires IZ2S";
#endif
    switch (app->state) {
        case INSTALLED: return "Installed";
        case UPDATE_AVAILABLE: return "Update available";
        case EXTERNAL: return "Installed separately";
        case UNAVAILABLE: return "Folder conflict";
        case SELF_ENTRY: return "IZ2S Cheese release";
        default: return "Ready to download";
    }
}

static const char *primary(const App *app) {
    if (!strcmp(app->package.id, "iz2s-cheese")) return NULL;
    if (app->state == NOT_INSTALLED) return "Download";
    if (app->state == UPDATE_AVAILABLE) return "Update";
    return NULL;
}

static bool managed(const App *app) {
    return strcmp(app->package.id, "iz2s-cheese") &&
        (app->state == INSTALLED || app->state == UPDATE_AVAILABLE);
}

static void action_button(Canvas *canvas, int x, int y, int width, const char *letter,
                          const char *caption, bool filled) {
    rounded(canvas,x,y,width,46,12,accent,accent);
    if (!filled) rounded(canvas,x+1,y+1,width-2,44,11,bar,bar);
    int text = text_width(canvas,SMALL,letter);
    int key_width = text > 18 ? text + 12 : 30;
    rounded(canvas,x+12,y+8,key_width,30,13,filled?background:edge,filled?background:edge);
    label(canvas,x+12+(key_width-text)/2,y+12,text+2,SMALL,accent,letter);
    int caption_x = 23+key_width;
    label(canvas,x+caption_x,y+7,width-caption_x-8,NORMAL,filled?background:ink,caption);
}

static void icon_path(Canvas *canvas, int x, int y, const uint8_t points[][2],
                      size_t count, uint32_t color) {
    for (size_t i = 1; i < count; ++i) {
        int col = points[i-1][0], row = points[i-1][1];
        int end_col = points[i][0], end_row = points[i][1];
        int sx = col < end_col ? 1 : -1, sy = row < end_row ? 1 : -1;
        int dx = (end_col-col)*sx, dy = (row-end_row)*sy;
        int error = dx+dy;
        for (;;) {
            rectangle(canvas,x+col-1,y+row-1,3,3,color);
            if (col == end_col && row == end_row) break;
            int twice = 2*error;
            if (twice >= dy) { error += dy; col += sx; }
            if (twice <= dx) { error += dx; row += sy; }
        }
    }
}

static void category_icon(Canvas *canvas, unsigned filter, int x, int y, uint32_t color) {
    switch (filter) {
        case FILTER_ALL:
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 2; ++col)
                    rounded(canvas,x+2+col*11,y+2+row*11,8,8,2,color,color);
            break;
        case FILTER_INSTALLED:
            outline(canvas,x+2,y+2,20,20,2,color);
            icon_path(canvas,x,y,(const uint8_t[][2]){{7,12},{10,15},{16,9}},3,color);
            break;
        case FILTER_UPDATES:
            icon_path(canvas,x,y,(const uint8_t[][2]){{3,10},{6,5},{12,3},{17,5},{20,8}},5,color);
            icon_path(canvas,x,y,(const uint8_t[][2]){{20,3},{20,8},{15,8}},3,color);
            icon_path(canvas,x,y,(const uint8_t[][2]){{20,13},{17,18},{12,20},{7,18},{3,15}},5,color);
            icon_path(canvas,x,y,(const uint8_t[][2]){{3,20},{3,15},{8,15}},3,color);
            break;
        case FILTER_TOOLS:
            icon_path(canvas,x,y,(const uint8_t[][2]){{4,20},{2,18},{10,10},{9,6},{12,3},{16,2},
                {13,6},{17,10},{21,7},{20,11},{17,14},{13,13},{6,21},{4,20}},14,color);
            break;
        case FILTER_MEDIA:
            outline(canvas,x+1,y+4,22,16,2,color);
            for (int i = 0; i < 6; ++i) rectangle(canvas,x+10+i,y+8+i/2,1,8-i,color);
            break;
        case FILTER_EMULATORS:
            outline(canvas,x+4,y+1,16,22,2,color);
            outline(canvas,x+7,y+4,10,8,2,color);
            rectangle(canvas,x+7,y+16,6,2,color);
            rectangle(canvas,x+9,y+14,2,6,color);
            rectangle(canvas,x+15,y+17,2,2,color);
            break;
        case FILTER_GAMES:
            icon_path(canvas,x,y,(const uint8_t[][2]){{7,6},{16,6},{19,8},{22,16},{20,18},{15,14},
                {8,14},{3,18},{1,16},{4,8},{7,6}},11,color);
            rectangle(canvas,x+5,y+10,6,2,color);
            rectangle(canvas,x+7,y+8,2,6,color);
            rectangle(canvas,x+14,y+10,2,2,color);
            rectangle(canvas,x+17,y+12,2,2,color);
            break;
    }
}

static void footer_key(Canvas *canvas, int x, const char *letter, const char *caption, bool primary_button) {
    int text = text_width(canvas,SMALL,letter), width = text+18;
    if (width < 33) width = 33;
    rounded(canvas,x,669,width,33,15,primary_button?accent:edge,primary_button?accent:edge);
    if (!primary_button) rounded(canvas,x+1,670,width-2,31,14,bar,bar);
    label(canvas,x+(width-text)/2,674,text+2,SMALL,primary_button?background:accent,letter);
    label(canvas,x+width+10,673,142,NORMAL,primary_button?ink:muted,caption);
}

static const char *kind(const App *app) { return app->package.kind == PACKAGE_IZM ? "IZA" : "NRO"; }
static uint32_t kind_color(const App *app) { return app->package.kind == PACKAGE_IZM ? accent : nro_ink; }

static void tile_title(Canvas *canvas, int x, int y, int width, const char *title) {
    const Face *font = &canvas->art->fonts[NORMAL];
    char text[72];
    size_t length = strlen(title);
    if (length >= sizeof text) length = sizeof text - 1;
    memcpy(text,title,length);
    text[length] = 0;
    unsigned pixels = 0;
    for (size_t i = 0; i < length; ++i) pixels += font->advance[glyph_index(text[i])];
    if ((int)pixels > width) {
        unsigned ellipsis = 3 * font->advance[glyph_index('.')];
        while (length && (int)(pixels + ellipsis) > width)
            pixels -= font->advance[glyph_index(text[--length])];
        memcpy(text+length,"...",4);
    }
    label(canvas,x,y,width,NORMAL,ink,text);
}

static void card(Canvas *canvas, int x, int y, int width, const App *app, bool focused) {
    static const uint32_t tones[] = {0xff94504d,0xff7a408e,0xff925b62,0xff6543a4,0xff847b36};
    unsigned color = 0;
    for (const unsigned char *ch = (const unsigned char *)app->package.id; *ch; ++ch) color = color*31 + *ch;
    rounded(canvas,x,y,width,CARD_H,16,focused?accent:edge,focused?accent:edge);
    int border = focused ? 3 : 1;
    rounded(canvas,x+border,y+border,width-2*border,CARD_H-2*border,14,
            tones[color%5],0xff170e22);
    enum { FORMAT_W = 48, FRAME_MAX = 166, FRAME_PAD = 6 };
    const char *format = kind(app);
    int format_x = x + width - FORMAT_W - 12;
    int format_width = text_ink_width(canvas,SMALL,format);
    rounded(canvas,format_x,y+14,FORMAT_W,28,7,bar,bar);
    label(canvas,format_x+(FORMAT_W-format_width)/2,y+16,FORMAT_W,SMALL,kind_color(app),format);
    int frame = width - 24;
    if (frame > FRAME_MAX) frame = FRAME_MAX;
    int frame_x = x + (width-frame)/2, frame_y = y + 137 - frame/2;
    rounded(canvas,frame_x,frame_y,frame,frame,14,0xff35223e,0xff28162f);
    cover(canvas,frame_x+FRAME_PAD,frame_y+FRAME_PAD,app->icon,frame-2*FRAME_PAD);
    rectangle(canvas,x+16,y+231,width-32,1,focused?accent:edge);
    tile_title(canvas,x+14,y+239,width-28,app->package.title);
    if (check_mark(app->state)) check(canvas,x+16,y+272);
    const char *status = app->state == UPDATE_AVAILABLE ? "Update" :
        app->state == EXTERNAL ? "External" : app->state == INSTALLED ? "Installed" :
        app->state == UNAVAILABLE ? "Unavailable" : app->package.version;
    label(canvas,x+(check_mark(app->state)?44:15),y+266,width-57,SMALL,
          check_mark(app->state)?success:muted,status);
}

static const char *repository_host(const char *url, char host[64]) {
    const char *start = url + 8;
    size_t size = strcspn(start, "/");
    if (size >= 64) size = 63;
    memcpy(host, start, size);
    host[size] = 0;
    if (!strcmp(host,"github.com")) return "GitHub";
    if (!strcmp(host,"gitlab.com")) return "GitLab";
    if (!strcmp(host,"codeberg.org")) return "Codeberg";
    if (!strcmp(host,"bitbucket.org")) return "Bitbucket";
    return host;
}

static void detail(Canvas *canvas, const App *app, const View *view) {
    label(canvas,CARD_X,48,790,LARGE,ink,app->package.title);
    rectangle(canvas,CARD_X,105,952,1,edge);
    rectangle(canvas,CARD_X,132,222,386,panel);
    cover(canvas,CARD_X+21,153,app->icon,180);
    label(canvas,CARD_X+22,356,178,NORMAL,kind_color(app),kind(app));
    label(canvas,CARD_X+22,400,188,SMALL,check_mark(app->state)?success:muted,state_name(app));
    if (check_mark(app->state)) check(canvas,CARD_X+23,448);

    int x = CARD_X + 258;
    label(canvas,x,133,640,MEDIUM,ink,"About");
    wrap(canvas,x,180,675,5,NORMAL,muted,app->package.description);
    char repository[RELEASE_URL_MAX], host[64];
    store_repository(&app->package, repository);
    if (repository[0]) {
        const char *platform = repository_host(repository, host);
        const char *project = strchr(repository+8, '/');
        rounded(canvas,x,333,670,66,12,edge,edge);
        rounded(canvas,x+1,334,668,64,11,panel,bar);
        label(canvas,x+17,339,560,SMALL,accent,platform);
        label(canvas,x+17,365,560,SMALL,ink,project ? project+1 : repository);
        rounded(canvas,x+615,350,32,32,14,edge,edge);
        label(canvas,x+625,354,18,SMALL,accent,"R");
    }
    label(canvas,x,414,350,SMALL,dim,"Publisher");
    label(canvas,x,440,350,NORMAL,ink,app->package.author);
    label(canvas,x+385,414,280,SMALL,dim,"Catalog version");
    label(canvas,x+385,440,280,NORMAL,ink,app->package.version);
    if (app->installed_version[0]) {
        char installed[80];
        snprintf(installed,sizeof installed,"Installed %s",app->installed_version);
        label(canvas,x+385,489,280,SMALL,dim,installed);
    }
    char size[72];
    uint64_t bytes = 0;
    for (unsigned i = 0; i < app->package.count; ++i) bytes += app->package.files[i].bytes;
    snprintf(size,sizeof size,"%.1f MiB  /  %s",(double)bytes / (1024.0 * 1024.0),
        app->package.release_url[0] ? "GitHub release" : "Package on SD");
    label(canvas,x,489,370,SMALL,muted,size);
    const char *action = primary(app);
    if (action && !view->busy) {
        rounded(canvas,x,550,194,46,11,accent,accent);
        rounded(canvas,x+12,558,30,30,13,background,background);
        label(canvas,x+21,562,18,SMALL,accent,"A");
        label(canvas,x+53,557,136,NORMAL,background,action);
    }
    if (managed(app) && !view->busy) {
        int remove_x = action ? x+207 : x;
        action_button(canvas,remove_x,550,180,"X","Remove",false);
    }
}

enum {
    SETTINGS_Y = 124, SETTINGS_STEP = 80, SETTINGS_W = 952, SETTINGS_H = 70,
    CATALOG_SETTINGS_Y = 112, CATALOG_SETTINGS_STEP = 54, CATALOG_SETTINGS_H = 49,
    CATALOG_TOGGLE_X = 1102, CATALOG_TOGGLE_W = 103
};

static const char raw_github_prefix[] = "https://raw.githubusercontent.com/";

static const char *catalog_address_label(const CatalogSource *source) {
    if (!strncmp(source->url, raw_github_prefix, sizeof raw_github_prefix - 1))
        return source->url + sizeof raw_github_prefix - 1;
    return source->url;
}

static const char *catalog_title(const CatalogSource *source, char title[RELEASE_URL_MAX]) {
    if (source->community) return "Switch 2 Homebrew Catalog";
    const char *owner = catalog_address_label(source);
    if (owner != source->url) {
        const char *repo = strchr(owner, '/');
        const char *end = repo ? strchr(repo + 1, '/') : NULL;
        if (end) {
            snprintf(title, RELEASE_URL_MAX, "%.*s", (int)(end - owner), owner);
            return title;
        }
    }
    const char *host = strstr(source->url, "://");
    host = host ? host + 3 : source->url;
    snprintf(title, RELEASE_URL_MAX, "%.*s", (int)strcspn(host, "/"), host);
    return title;
}

static void settings(Canvas *canvas, const Store *store, const View *view) {
    label(canvas,CARD_X,34,850,LARGE,ink,"Settings");
    rectangle(canvas,CARD_X,97,SETTINGS_W,1,edge);
    const char *titles[SETTINGS_COUNT] = {
        "Catalogs", "Refresh catalogs", "Test GitHub connection", "Check for Cheese updates", "About"
    };
    const App *self = store_self_entry(store);
    const char *update_status = !store->update_checked ? "Check the latest official release" :
        self && self->state == UPDATE_AVAILABLE ? "An official Cheese update is available" :
        "Latest stable release checked";
    char source_status[96];
    snprintf(source_status, sizeof source_status, "%u of %u sources enabled",
             store_enabled_online_catalogs(&view->settings) + view->settings.sd_enabled, view->settings.count + 1);
    const char *descriptions[SETTINGS_COUNT] = {
        source_status,
        "Update apps and thumbnails from enabled sources",
        view->github[0] ? view->github : "Check DNS, HTTPS and the GitHub release server",
        update_status, "Project and author links"
    };
    for (unsigned i = 0; i < SETTINGS_COUNT; ++i) {
        int y = SETTINGS_Y + (int)i * SETTINGS_STEP;
        rectangle(canvas,CARD_X,y,SETTINGS_W,SETTINGS_H,panel);
        outline(canvas,CARD_X,y,SETTINGS_W,SETTINGS_H,view->settings_row == i ? 2 : 1,
                view->settings_row == i ? accent : edge);
        label(canvas,CARD_X+18,y+8,880,NORMAL,ink,titles[i]);
        label(canvas,CARD_X+18,y+37,908,SMALL,muted,descriptions[i]);
        if (i == SETTINGS_CATALOGS) label(canvas,1191,y+18,21,NORMAL,accent,">");
    }
    label(canvas,CARD_X,540,SETTINGS_W,SMALL,muted,view->network[0] ? view->network : "Switch system connection");
    label(canvas,CARD_X,565,SETTINGS_W,SMALL,dim,view->addresses);
}

static void catalog_settings(Canvas *canvas, const Store *store, const View *view) {
    label(canvas,CARD_X,30,850,LARGE,ink,"Catalogs");
    char count[96];
    snprintf(count, sizeof count, "%u of %u sources enabled",
             store_enabled_online_catalogs(&view->settings) + view->settings.sd_enabled, view->settings.count + 1);
    label(canvas,CARD_X,78,SETTINGS_W,SMALL,muted,count);
    for (unsigned i = 0; i <= view->settings.count; ++i) {
        bool sd = i == view->settings.count;
        const CatalogSource *source = sd ? NULL : &view->settings.sources[i];
        bool selected = i == view->catalog_row;
        bool enabled = sd ? view->settings.sd_enabled : source->enabled;
        int y = CATALOG_SETTINGS_Y + (int)i * CATALOG_SETTINGS_STEP;
        rectangle(canvas,CARD_X,y,SETTINGS_W,CATALOG_SETTINGS_H,panel);
        outline(canvas,CARD_X,y,SETTINGS_W,CATALOG_SETTINGS_H,selected ? 2 : 1,selected ? accent : edge);
        char title[RELEASE_URL_MAX];
        const char *name = sd ? "SD card" : catalog_title(source, title);
        const char *description = sd ? (store->local_catalog_available ? "Local catalog.tsv found" :
            "Copy catalog.tsv to SD:" STORE_CATALOG_DIR "/") : catalog_address_label(source);
        label(canvas,CARD_X+18,y,778,NORMAL,ink,name);
        label(canvas,CARD_X+18,y+24,794,SMALL,muted,description);
        uint32_t color = enabled ? success : bar;
        rounded(canvas,CATALOG_TOGGLE_X,y+8,CATALOG_TOGGLE_W,33,8,color,color);
        const char *status = enabled ? "On" : "Off";
        int width = text_width(canvas,SMALL,status);
        label(canvas,CATALOG_TOGGLE_X+(CATALOG_TOGGLE_W-width)/2,y+12,width+2,SMALL,
              enabled ? background : muted,status);
    }
}

static Command empty_catalog_action(const View *view) {
    if (store_enabled_online_catalogs(&view->settings)) return REFRESH;
    if (store_community_catalog(&view->settings) < view->settings.count) return ENABLE_COMMUNITY;
    return NO_JOB;
}

static void empty_catalog(Canvas *canvas, const View *view, int left) {
    Command action = empty_catalog_action(view);
    rounded(canvas,left,154,1234-left,192,14,panel,bar);
    const char *title = action == REFRESH ? "Catalog unavailable" : "Browse homebrew";
    const char *description = action == REFRESH ? "Retry the connection or choose another catalog." :
        action == ENABLE_COMMUNITY ? "Open the Switch 2 Homebrew Catalog to see available apps." :
        "Choose or add a source to see available apps.";
    label(canvas,left+WELCOME_INSET,177,850,MEDIUM,ink,title);
    label(canvas,left+WELCOME_INSET,223,850,NORMAL,muted,description);
    int catalog_x = left + WELCOME_INSET;
    if (action != NO_JOB) {
        action_button(canvas,catalog_x,WELCOME_ACTION_Y,WELCOME_PRIMARY_W,
                      action == REFRESH ? "Y" : "A",action == REFRESH ? "Retry" : "Open community catalog",true);
        catalog_x += WELCOME_PRIMARY_W + WELCOME_GAP;
    }
    action_button(canvas,catalog_x,WELCOME_ACTION_Y,WELCOME_CATALOGS_W,"X","Catalogs",action == NO_JOB);
}

static bool settings_screen(Screen screen) {
    return screen == SETTINGS || screen == CATALOG_SETTINGS || screen == SETTINGS_RESULT;
}

static void draw_github_mark(Canvas *canvas, int x, int y, uint32_t color) {
    for (int row = 0; row < 24; ++row)
        for (int col = 0; col < 24; ++col) {
            uint32_t *pixel = &canvas->pixels[(y+row)*canvas->stride+x+col];
            *pixel = blend(*pixel,color,github_mark[row][col]);
        }
}

static void dialog(Canvas *canvas, const Store *store, const App *app, const View *view) {
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            uint32_t *pixel = &canvas->pixels[y*canvas->stride+x];
            uint32_t alpha = *pixel & 0xff000000u;
            *pixel = (blend(*pixel,background,177) & 0x00ffffffu) | alpha;
        }
    if (view->screen == ABOUT) {
        rounded(canvas,220,114,840,472,16,panel,bar);
        label(canvas,252,140,760,LARGE,ink,"IZ2S Cheese");
        label(canvas,252,200,760,NORMAL,muted,"Developer: IZMYSH");
        const struct { const char *title, *url; } links[] = {
            {"Author", "https://github.com/byizmysh"},
            {"IZ2S Cheese", "https://github.com/byizmysh/iz2s-cheese"},
            {"IZ2S Platform", "https://github.com/byizmysh/IZ2S-Platform"}
        };
        for (unsigned i = 0; i < sizeof links / sizeof links[0]; ++i) {
            int y = 247 + (int)i * 81;
            rounded(canvas,252,y,776,67,10,panel,bar);
            draw_github_mark(canvas,268,y+21,ink);
            label(canvas,310,y+6,700,SMALL,muted,links[i].title);
            label(canvas,310,y+29,700,NORMAL,ink,links[i].url);
        }
        action_button(canvas,795,508,232,"B","Back",false);
        return;
    }
    if (view->screen == REPOSITORY && app && (!view->busy || view->minimized)) {
        char url[RELEASE_URL_MAX], host[64];
        store_repository(&app->package,url);
        rounded(canvas,102,80,1076,532,16,panel,bar);
        label(canvas,132,105,1016,MEDIUM,ink,"Repository");
        label(canvas,132,157,1016,NORMAL,accent,repository_host(url,host));
        label(canvas,132,196,1016,NORMAL,ink,app->package.title);
        wrap(canvas,132,232,1016,12,SMALL,muted,url);
        action_button(canvas,920,549,225,"B","Back",false);
        return;
    }
    if (!view->busy && view->screen == SETTINGS_RESULT) {
        rounded(canvas,182,150,916,420,16,panel,bar);
        label(canvas,212,180,856,MEDIUM,ink,view->result_title);
        wrap(canvas,212,240,856,6,NORMAL,view->failure?warning:success,view->message);
        label(canvas,212,440,856,SMALL,muted,view->network);
        label(canvas,212,472,856,SMALL,muted,view->addresses);
        const App *self = store_self_entry(store);
        if (view->update_result && self && self->state == UPDATE_AVAILABLE)
            action_button(canvas,590,510,225,"A","Update",true);
        action_button(canvas,842,510,225,"B","Back",false);
        return;
    }
    if (view->busy && view->source[0]) {
        rounded(canvas,102,80,1076,532,16,panel,bar);
        label(canvas,132,105,1016,MEDIUM,ink,view->message);
        label(canvas,132,150,1016,SMALL,dim,"Source");
        wrap(canvas,132,185,1016,13,SMALL,muted,view->source);
        rectangle(canvas,132,520,1016,12,edge);
        rectangle(canvas,132,520,(int)view->percent*1016/100,12,success);
        char progress[24];
        snprintf(progress,sizeof progress,"%u%%",view->percent);
        label(canvas,132,550,120,NORMAL,muted,progress);
        action_button(canvas,670,549,225,"Y","Minimize",false);
        if (view->cancellable) {
            action_button(canvas,920,549,225,"B","Cancel",false);
        }
        return;
    }
    rectangle(canvas,346,228,588,264,panel);
    outline(canvas,346,228,588,264,2,edge);
    if (view->busy) {
        label(canvas,376,254,540,MEDIUM,ink,view->message);
        rectangle(canvas,376,350,528,12,bar);
        rectangle(canvas,376,350,(int)view->percent*528/100,12,success);
        if (view->percent) {
            char progress[24];
            snprintf(progress,sizeof progress,"%u%%",view->percent);
            label(canvas,376,382,106,NORMAL,muted,progress);
        }
        return;
    }
    if (view->screen == CONFIRM_COMPATIBILITY) {
        label(canvas,376,254,530,MEDIUM,ink,"IZA compatibility");
        wrap(canvas,376,308,528,3,NORMAL,muted,
            "This app needs a compatible IZ2S runtime. A homebrew menu that only runs NRO cannot open IZA apps.");
        action_button(canvas,366,415,220,"A","Continue",true);
        action_button(canvas,660,415,235,"B","Cancel",false);
        return;
    }
    label(canvas,376,254,530,MEDIUM,ink,"Remove this app?");
    label(canvas,376,312,531,NORMAL,muted,app->package.title);
    label(canvas,376,348,531,SMALL,muted,"Saved data stays on the SD card.");
    action_button(canvas,366,415,220,"A","Remove",true);
    action_button(canvas,660,415,235,"B","Cancel",false);
}

void view_draw(uint32_t *pixels, unsigned stride, const Artwork *art, const Store *store, const View *view) {
    Canvas canvas = {pixels,stride,art};
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        uint32_t left = blend(0xff521a62,0xff392154,(unsigned)(255*y/SCREEN_HEIGHT));
        uint32_t right = blend(0xff59294d,0xff702b21,(unsigned)(255*y/SCREEN_HEIGHT));
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            pixels[y*stride+x] = blend(left,right,(unsigned)(255*x/SCREEN_WIDTH));
    }
    int rail = rail_width(view, store);
    bool compact = rail == RAIL_COMPACT;
    rounded(&canvas,12,16,rail-20,625,18,sidebar,bar);
    const char *brand_title = "IZ2S Cheese";
    int title_width = text_width(&canvas,NORMAL,brand_title);
    int group_width = BRAND_SIZE;
    if (!compact) group_width += 12 + text_ink_width(&canvas,NORMAL,brand_title);
    if (group_width > rail-20) group_width = rail-20;
    int brand_x = 12 + (rail-20-group_width)/2;
    const int brand_y = 32;
    if (art->brand_loaded) {
        for (int row = 0; row < BRAND_SIZE; ++row)
            for (int col = 0; col < BRAND_SIZE; ++col) {
                uint32_t icon = art->brand[row][col];
                uint32_t *target = &pixels[(brand_y+row)*stride+brand_x+col];
                *target = blend(*target, icon, icon >> 24);
            }
    } else label(&canvas,brand_x+8,brand_y+9,BRAND_SIZE-8,MEDIUM,accent,"IZ");
    if (!compact) {
        int text_x = brand_x+BRAND_SIZE+12;
        label(&canvas,text_x,brand_y+5,title_width+2,NORMAL,ink,brand_title);
        label(&canvas,text_x,brand_y+29,title_width+2,SMALL,muted,"Homebrew");
    }
    if (view->screen == CATALOG && store->count) {
        for (unsigned i = 0; i < FILTERS; ++i) {
            int y = RAIL_ROW_Y + (int)i*RAIL_ROW_STEP;
            bool active = view->filter == i;
            uint32_t color = active ? background : muted;
            if (active) rounded(&canvas,compact?18:22,y,rail-(compact?32:40),RAIL_ROW_H,10,accent,accent);
            category_icon(&canvas,i,compact?31:33,y+10,color);
            if (!compact) label(&canvas,70,y+8,rail-90,NORMAL,color,filters[i]);
        }
    }
    if (!compact) {
        draw_github_mark(&canvas,31,601,muted);
        label(&canvas,65,602,153,SMALL,muted,"@byizmysh");
    }
    const App *app = view->count ? &store->apps[view->visible[view->cursor]] : NULL;
    bool modal = view->screen == ABOUT || view->screen == REPOSITORY ||
        view->screen == SETTINGS_RESULT ||
        (view->busy && !view->minimized) ||
        (app && (view->screen == CONFIRM_REMOVE || view->screen == CONFIRM_COMPATIBILITY));
    if (view->screen == CATALOG_SETTINGS) catalog_settings(&canvas,store,view);
    else if (settings_screen(view->screen) || view->screen == ABOUT) settings(&canvas,store,view);
    else if (app && view->screen != CATALOG) detail(&canvas,app,view);
    else {
        int left = catalog_x(rail), width = card_width(left);
        label(&canvas,left,view->search[0]?24:42,SEARCH_X-left-20,LARGE,ink,filters[view->filter]);
        if (view->search[0] && store->count) {
            char results[32];
            snprintf(results,sizeof results,"%u %s",view->count,view->count == 1 ? "match" : "matches");
            int results_width = text_width(&canvas,SMALL,results);
            label(&canvas,left,72,results_width+2,SMALL,accent,results);
            int query_x = left+results_width+18;
            label(&canvas,query_x,72,SEARCH_X-query_x-20,SMALL,muted,view->search);
        }
        if (store->count && !view->busy) {
            action_button(&canvas,SEARCH_X,HEADER_ACTION_Y,SEARCH_W,"ZR",view->search[0]?"Edit search":"Search",false);
            action_button(&canvas,CATALOGS_X,HEADER_ACTION_Y,CATALOGS_W,"X","Catalogs",false);
        }
        rectangle(&canvas,left,104,1234-left,1,edge);
        unsigned start = view->cursor / PAGE_SIZE * PAGE_SIZE;
        for (unsigned i = 0; i < PAGE_SIZE && start+i < view->count; ++i)
            card(&canvas,left+(int)i*(width+CARD_GAP),CARD_Y,width,
                 &store->apps[view->visible[start+i]],start+i == view->cursor);
        if (app) {
            rounded(&canvas,left,463,1234-left,112,14,panel,bar);
            int title_x = left+20, title_room = 545;
            label(&canvas,title_x,478,title_room,MEDIUM,ink,app->package.title);
            int title_width = text_width(&canvas,MEDIUM,app->package.title);
            int author_x = title_x + (title_width < title_room ? title_width : title_room) + 16;
            if (author_x < 880) {
                rounded(&canvas,author_x,491,5,5,2,accent,accent);
                label(&canvas,author_x+15,485,880-author_x,SMALL,dim,app->package.author);
            }
            wrap(&canvas,left+20,511,1234-left-42,2,NORMAL,muted,app->package.description);
            label(&canvas,905,485,288,SMALL,accent,state_name(app));
        } else if (!view->busy) {
            if (store->count) label(&canvas,left,170,940,MEDIUM,muted,
                view->search[0] ? "No apps match this search" : "No applications in this view");
            else empty_catalog(&canvas,view,left);
        }
        if (view->count > PAGE_SIZE) {
            int page_y = view->busy && view->minimized ? 576 : PAGE_Y;
            int page_height = view->busy && view->minimized ? 30 : PAGE_BUTTON_H;
            int text_y = view->busy && view->minimized ? page_y+1 : page_y+7;
            char pages[32];
            snprintf(pages,sizeof pages,"%u / %u",start/PAGE_SIZE+1,(view->count+PAGE_SIZE-1)/PAGE_SIZE);
            label(&canvas,1100,text_y,72,NORMAL,muted,pages);
            if (start) {
                rounded(&canvas,PAGE_PREV_X,page_y,PAGE_BUTTON_W,page_height,9,panel,bar);
                label(&canvas,PAGE_PREV_X+17,text_y,24,NORMAL,ink,"<");
            }
            if (start + PAGE_SIZE < view->count) {
                rounded(&canvas,PAGE_NEXT_X,page_y,PAGE_BUTTON_W,page_height,9,panel,bar);
                label(&canvas,PAGE_NEXT_X+17,text_y,24,NORMAL,ink,">");
            }
        }
    }
    if (view->busy && view->minimized) {
        rounded(&canvas,CARD_X,608,952,39,11,panel,bar);
        label(&canvas,CARD_X+14,612,130,SMALL,accent,view->cancellable?"Downloading":"Cancelling...");
        label(&canvas,CARD_X+150,612,430,SMALL,ink,view->download_title);
        rectangle(&canvas,864,625,190,4,edge);
        rectangle(&canvas,864,625,(int)view->percent*190/100,4,success);
        char percent[16];
        snprintf(percent,sizeof percent,"%u%%",view->percent);
        label(&canvas,1068,612,65,SMALL,muted,percent);
        label(&canvas,1142,612,80,SMALL,accent,"Y Open");
    }
    if (!view->busy && view->message[0] && view->screen != SETTINGS_RESULT)
        label(&canvas,CARD_X,619,952,SMALL,view->failure?warning:success,view->message);
    rectangle(&canvas,0,654,SCREEN_WIDTH,66,bar);
    rectangle(&canvas,0,654,SCREEN_WIDTH,1,edge);
    if (modal) {
        dialog(&canvas,store,app,view);
        return;
    }
    if (view->screen == CATALOG) {
        if (store->count) {
            footer_key(&canvas,27,"L / R","Categories",false);
            footer_key(&canvas,264,"ZL",compact?"Expand":"Compact",false);
        }
        if (store->count || view->busy) footer_key(&canvas,490,"Y",view->busy?"Download":"Refresh",false);
        if (view->count) footer_key(&canvas,773,"A","Details",true);
        footer_key(&canvas,916,"-","Settings",false);
        footer_key(&canvas,1071,"B",view->busy?"Download":view->search[0]?"Clear search":"Exit",false);
    } else if (view->screen == CATALOG_SETTINGS) {
        if (view->settings.count < STORE_MAX_CATALOGS) footer_key(&canvas,272,"X","Add URL",false);
        if (view->catalog_row == view->settings.count) {
            footer_key(&canvas,826,"A",view->settings.sd_enabled ? "Disable" : "Enable",true);
        } else if (view->catalog_row < view->settings.count) {
            const CatalogSource *source = &view->settings.sources[view->catalog_row];
            if (!source->community) {
                footer_key(&canvas,453,"Y","Edit URL",false);
                footer_key(&canvas,628,"R","Remove",false);
            }
            footer_key(&canvas,826,"A",source->enabled ? "Disable" : "Enable",true);
        }
        footer_key(&canvas,1071,"B","Back",false);
    } else if (view->screen == SETTINGS) {
        if (view->busy) footer_key(&canvas,824,"Y","Download",true);
        else footer_key(&canvas,824,"A","Select",true);
        footer_key(&canvas,1071,"B","Back",false);
    } else {
        if (view->busy) footer_key(&canvas,824,"Y","Download",true);
        else if (view->screen == DETAILS && app) {
            char url[RELEASE_URL_MAX];
            store_repository(&app->package,url);
            if (url[0]) footer_key(&canvas,824,"R","Repository",false);
        }
        footer_key(&canvas,1071,"B","Back",false);
    }
}

static bool selected_filter(unsigned filter, const App *app) {
    switch (filter) {
        case FILTER_INSTALLED: return check_mark(app->state);
        case FILTER_UPDATES: return app->state == UPDATE_AVAILABLE;
        case FILTER_TOOLS: return !strcmp(app->package.category,"Tools");
        case FILTER_MEDIA: return !strcmp(app->package.category,"Media");
        case FILTER_EMULATORS: return !strcmp(app->package.category,"Emulators");
        case FILTER_GAMES: return !strcmp(app->package.category,"Games");
        default: return true;
    }
}

static unsigned char search_fold(unsigned char ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
}

static bool search_contains(const char *text, const char *word, size_t length) {
    for (; *text; ++text) {
        size_t i = 0;
        while (i < length && text[i] && search_fold(text[i]) == search_fold(word[i])) ++i;
        if (i == length) return true;
    }
    return false;
}

static const char search_spaces[] = " \t\r\n";

static bool search_matches(const Package *package, const char *query) {
    while (*query) {
        size_t length = strcspn(query,search_spaces);
        if (!search_contains(package->title,query,length) &&
            !search_contains(package->author,query,length) &&
            !search_contains(package->id,query,length)) return false;
        query += length;
        query += strspn(query,search_spaces);
    }
    return true;
}

void view_search(View *view, const Store *store, const char *query) {
    query += strspn(query,search_spaces);
    size_t length = strlen(query);
    while (length && strchr(search_spaces,query[length-1])) --length;
    if (length >= sizeof view->search) {
        length = sizeof view->search - 1;
        while (((unsigned char)query[length] & 0xc0) == 0x80) --length;
    }
    if (length && !view->search[0]) view->filter = FILTER_ALL;
    memcpy(view->search,query,length);
    view->search[length] = 0;
    view->cursor = 0;
    view_filter(view,store);
}

void view_filter(View *view, const Store *store) {
    view->count = 0;
    if (view->visible_capacity < store->count) {
        unsigned *next = malloc((size_t)store->count * sizeof *next);
        if (!next) { view->cursor = 0; view->failure = true; snprintf(view->message, sizeof view->message, "Not enough memory to display catalog."); return; }
        free(view->visible); view->visible = next; view->visible_capacity = store->count;
    }
    for (unsigned i = 0; i < store->count; ++i)
        if (
#ifdef IZ_STORE_STANDALONE
            store->apps[i].package.kind != PACKAGE_IZM &&
#endif
            (strcmp(store->apps[i].package.id, "iz2s-cheese") ||
#ifdef IZ_STORE_STANDALONE
             store->apps[i].package.kind == PACKAGE_NRO) &&
#else
             store->apps[i].package.kind == PACKAGE_IZM) &&
#endif
            selected_filter(view->filter, &store->apps[i]) &&
            search_matches(&store->apps[i].package, view->search)) view->visible[view->count++] = i;
    if (view->cursor >= view->count) view->cursor = view->count ? view->count-1 : 0;
    if (!view->count && !settings_screen(view->screen) && view->screen != ABOUT) view->screen = CATALOG;
}

static void open_catalogs(View *view) {
    view->catalogs_back = view->screen;
    view->screen = CATALOG_SETTINGS;
}

static Command settings_action(View *view) {
    switch (view->settings_row) {
    case SETTINGS_CHEESE_UPDATE:
        view->update_result = false;
        return CHECK_STORE_UPDATE;
    case SETTINGS_CATALOGS: open_catalogs(view); break;
    case SETTINGS_NETWORK: return CHECK_NETWORK;
    case SETTINGS_ABOUT: view->screen = ABOUT; break;
    case SETTINGS_DOWNLOAD:
        if (view->settings.sd_enabled || store_enabled_online_catalogs(&view->settings)) return FETCH_LIST;
        open_catalogs(view);
        break;
    }
    return NO_JOB;
}

Command view_input(View *view, const Store *store, unsigned buttons) {
    if (view->busy) {
        if (!view->source[0]) return NO_JOB;
        if (buttons & BTN_Y) { view->minimized = !view->minimized; return NO_JOB; }
        if (!view->minimized)
            return view->cancellable && (buttons & BTN_B) ? CANCEL_DOWNLOAD : NO_JOB;
        if ((buttons & BTN_PLUS) || ((buttons & BTN_B) && view->screen == CATALOG)) {
            view->minimized = false;
            return NO_JOB;
        }
        buttons &= ~(BTN_X | BTN_ZR);
        if (view->screen == DETAILS || settings_screen(view->screen)) buttons &= ~BTN_A;
        if (view->screen == CATALOG_SETTINGS) buttons &= ~BTN_R;
    }
    if (!buttons) return NO_JOB;
    if (view->screen == ABOUT) {
        if (buttons & (BTN_B | BTN_X | BTN_PLUS)) view->screen = SETTINGS;
        return NO_JOB;
    }
    if (view->screen == REPOSITORY) {
        if (buttons & (BTN_B | BTN_R | BTN_PLUS)) view->screen = DETAILS;
        return NO_JOB;
    }
    if (view->screen == SETTINGS_RESULT) {
        if (buttons & (BTN_B | BTN_PLUS)) {
            view->screen = SETTINGS;
            view->update_result = false;
        } else if (buttons & BTN_A) {
            const App *self = store_self_entry(store);
            if (view->update_result && self && self->state == UPDATE_AVAILABLE) return UPDATE_STORE;
            view->screen = SETTINGS;
        }
        return NO_JOB;
    }
    if (view->screen == CATALOG_SETTINGS) {
        if (buttons & (BTN_B | BTN_MINUS | BTN_PLUS)) view->screen = view->catalogs_back;
        else if ((buttons & BTN_UP) && view->catalog_row) --view->catalog_row;
        else if ((buttons & BTN_DOWN) && view->catalog_row < view->settings.count) ++view->catalog_row;
        else if ((buttons & BTN_X) && view->settings.count < STORE_MAX_CATALOGS) return ADD_CATALOG;
        else if ((buttons & BTN_A) && view->catalog_row <= view->settings.count) return TOGGLE_CATALOG;
        else if (view->catalog_row < view->settings.count) {
            const CatalogSource *source = &view->settings.sources[view->catalog_row];
            if (!source->community) {
                if (buttons & BTN_Y) return EDIT_CATALOG;
                if (buttons & BTN_R) return REMOVE_CATALOG;
            }
        }
        return NO_JOB;
    }
    if (view->screen == CONFIRM_REMOVE || view->screen == CONFIRM_COMPATIBILITY) {
        Command confirmed = view->screen == CONFIRM_REMOVE ? REMOVE : INSTALL;
        if (buttons & BTN_B) view->screen = DETAILS;
        else if (buttons & BTN_A) { view->screen = DETAILS; return confirmed; }
        return NO_JOB;
    }
    if (buttons & BTN_PLUS) return EXIT;
    if ((buttons & BTN_ZL) && view->screen == CATALOG && store->count) { view->compact = !view->compact; return NO_JOB; }
    if (view->screen == SETTINGS) {
        if (buttons & (BTN_B | BTN_MINUS)) view->screen = CATALOG;
        else if ((buttons & BTN_UP) && view->settings_row) --view->settings_row;
        else if ((buttons & BTN_DOWN) && view->settings_row+1 < SETTINGS_COUNT) ++view->settings_row;
        else if (buttons & BTN_A) return settings_action(view);
        return NO_JOB;
    }
    if (view->screen == DETAILS) {
        const App *app = &store->apps[view->visible[view->cursor]];
        if (buttons & BTN_B) view->screen = CATALOG;
        else if (buttons & BTN_R) {
            char url[RELEASE_URL_MAX];
            store_repository(&app->package,url);
            if (url[0]) view->screen = REPOSITORY;
        }
        else if ((buttons & BTN_X) && managed(app)) view->screen = CONFIRM_REMOVE;
        else if ((buttons & BTN_A) && primary(app)) {
            if (view->inside_iz2s || app->package.kind != PACKAGE_IZM) return INSTALL;
            view->screen = CONFIRM_COMPATIBILITY;
        }
        return NO_JOB;
    }
    if (buttons & BTN_B) {
        if (!view->search[0]) return EXIT;
        view_search(view,store,"");
        return NO_JOB;
    }
    if (buttons & BTN_MINUS) { view->screen = SETTINGS; return NO_JOB; }
    if (buttons & BTN_X) { open_catalogs(view); return NO_JOB; }
    if (buttons & BTN_Y) return REFRESH;
    if ((buttons & BTN_A) && !store->count && empty_catalog_action(view) == ENABLE_COMMUNITY) {
        view->filter = FILTER_ALL;
        return ENABLE_COMMUNITY;
    }
    if (!store->count) return NO_JOB;
    if (buttons & BTN_ZR) return EDIT_SEARCH;
    if (buttons & (BTN_L | BTN_R)) {
        view->filter = (view->filter + (buttons & BTN_R ? 1 : FILTERS-1)) % FILTERS;
        view->cursor = 0;
        view_filter(view,store);
    }
    if ((buttons & BTN_UP) && view->cursor >= PAGE_SIZE) view->cursor -= PAGE_SIZE;
    if ((buttons & BTN_DOWN) && view->cursor + PAGE_SIZE < view->count) view->cursor += PAGE_SIZE;
    if ((buttons & BTN_LEFT) && view->cursor) --view->cursor;
    if ((buttons & BTN_RIGHT) && view->cursor + 1 < view->count) ++view->cursor;
    if ((buttons & BTN_A) && view->count) view->screen = DETAILS;
    return NO_JOB;
}

static bool hit(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left+width && y >= top && y < top+height;
}

Command view_touch(View *view, const Store *store, int x, int y) {
    if (view->busy) {
        if (!view->source[0]) return NO_JOB;
        if (!view->minimized) {
            if (hit(x,y,670,549,225,46)) return view_input(view,store,BTN_Y);
            return view->cancellable && hit(x,y,920,549,225,46) ? CANCEL_DOWNLOAD : NO_JOB;
        }
        if (hit(x,y,CARD_X,608,952,39)) return view_input(view,store,BTN_Y);
    }
    if (view->screen == CONFIRM_REMOVE || view->screen == CONFIRM_COMPATIBILITY) {
        if (hit(x,y,366,415,220,46)) return view_input(view,store,BTN_A);
        if (hit(x,y,660,415,235,46)) return view_input(view,store,BTN_B);
        return NO_JOB;
    }
    if (view->screen == REPOSITORY) {
        if (hit(x,y,920,549,225,46)) return view_input(view,store,BTN_B);
        return NO_JOB;
    }
    if (view->screen == ABOUT) {
        if (hit(x,y,795,508,232,46) || hit(x,y,1062,654,218,66)) return view_input(view,store,BTN_B);
        return NO_JOB;
    }
    if (view->screen == SETTINGS_RESULT) {
        const App *self = store_self_entry(store);
        if (view->update_result && self && self->state == UPDATE_AVAILABLE &&
            hit(x,y,590,510,225,46)) return view_input(view,store,BTN_A);
        if (hit(x,y,842,510,225,46)) return view_input(view,store,BTN_B);
        return NO_JOB;
    }
    if (hit(x,y,1062,654,218,66)) return view_input(view,store,BTN_B);
    if (view->screen == CATALOG_SETTINGS) {
        if (hit(x,y,264,654,170,66)) return view_input(view,store,BTN_X);
        if (hit(x,y,445,654,165,66)) return view_input(view,store,BTN_Y);
        if (hit(x,y,620,654,165,66)) return view_input(view,store,BTN_R);
        if (hit(x,y,818,654,196,66)) return view_input(view,store,BTN_A);
        for (unsigned i = 0; i <= view->settings.count; ++i) {
            int top = CATALOG_SETTINGS_Y+(int)i*CATALOG_SETTINGS_STEP;
            if (!hit(x,y,CARD_X,top,SETTINGS_W,CATALOG_SETTINGS_H)) continue;
            view->catalog_row = i;
            if (hit(x,y,CATALOG_TOGGLE_X,top,CATALOG_TOGGLE_W,CATALOG_SETTINGS_H))
                return view_input(view,store,BTN_A);
            break;
        }
        return NO_JOB;
    }
    if (view->screen == SETTINGS) {
        if (hit(x,y,813,654,190,66)) return view_input(view,store,BTN_A);
        for (unsigned i = 0; i < SETTINGS_COUNT; ++i)
            if (hit(x,y,CARD_X,SETTINGS_Y+(int)i*SETTINGS_STEP,SETTINGS_W,SETTINGS_H)) {
                view->settings_row = i;
                return view->busy ? NO_JOB : settings_action(view);
            }
        return NO_JOB;
    }
    if (view->screen == DETAILS) {
        const App *app = &store->apps[view->visible[view->cursor]];
        if (hit(x,y,530,333,670,66)) return view_input(view,store,BTN_R);
        bool download = primary(app) != NULL;
        if (download && hit(x,y,530,550,194,46)) return view_input(view,store,BTN_A);
        if (managed(app) && hit(x,y,download?737:530,550,180,46)) return view_input(view,store,BTN_X);
        return NO_JOB;
    }
    int rail = rail_width(view, store);
    if (hit(x,y,901,654,156,66)) return view_input(view,store,BTN_MINUS);
    if (hit(x,y,260,654,145,66)) return view_input(view,store,BTN_ZL);
    if (view->count && hit(x,y,768,654,126,66)) return view_input(view,store,BTN_A);
    if (hit(x,y,24,654,195,66)) return view_input(view,store,BTN_R);
    if (store->count && hit(x,y,SEARCH_X,HEADER_ACTION_Y,SEARCH_W,46)) return view_input(view,store,BTN_ZR);
    if (store->count && hit(x,y,CATALOGS_X,HEADER_ACTION_Y,CATALOGS_W,46)) return view_input(view,store,BTN_X);
    if (!view->busy && !store->count) {
        Command action = empty_catalog_action(view);
        int left = catalog_x(rail) + WELCOME_INSET;
        if (action != NO_JOB) {
            if (hit(x,y,left,WELCOME_ACTION_Y,WELCOME_PRIMARY_W,46))
                return view_input(view,store,action == REFRESH ? BTN_Y : BTN_A);
            left += WELCOME_PRIMARY_W + WELCOME_GAP;
        }
        if (hit(x,y,left,WELCOME_ACTION_Y,WELCOME_CATALOGS_W,46)) return view_input(view,store,BTN_X);
    }
    if (store->count && hit(x,y,14,RAIL_ROW_Y,rail-22,FILTERS*RAIL_ROW_STEP) &&
        (y-RAIL_ROW_Y)%RAIL_ROW_STEP < RAIL_ROW_H) {
        view->filter = (unsigned)(y-RAIL_ROW_Y)/RAIL_ROW_STEP;
        view->cursor = 0;
        view_filter(view,store);
    }
    if ((store->count || view->busy) && hit(x,y,479,654,195,66)) return view_input(view,store,BTN_Y);
    unsigned start = view->cursor / PAGE_SIZE * PAGE_SIZE;
    int page_y = view->busy && view->minimized ? 576 : PAGE_Y;
    int page_height = view->busy && view->minimized ? 30 : PAGE_BUTTON_H;
    if (start && hit(x,y,PAGE_PREV_X,page_y,PAGE_BUTTON_W,page_height)) {
        view->cursor = start - PAGE_SIZE;
        return NO_JOB;
    }
    if (start + PAGE_SIZE < view->count && hit(x,y,PAGE_NEXT_X,page_y,PAGE_BUTTON_W,page_height)) {
        view->cursor = start + PAGE_SIZE;
        return NO_JOB;
    }
    int left = catalog_x(rail), width = card_width(left);
    for (unsigned i = 0; i < PAGE_SIZE && start+i < view->count; ++i)
        if (hit(x,y,left+(int)i*(width+CARD_GAP),CARD_Y,width,CARD_H)) {
            view->cursor = start+i;
            view->screen = DETAILS;
            break;
        }
    return NO_JOB;
}

bool artwork_open(Artwork *art, const char *folder) {
    char path[STORE_PATH];
    if (snprintf(path,sizeof path,"%s/font.bin",folder) >= (int)sizeof path) return false;
    FILE *file = fopen(path,"rb");
    if (!file) return false;
    bool ok = fread(art->fonts,sizeof art->fonts,1,file) == 1 && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (snprintf(path,sizeof path,"%s/brand.rgba",folder) < (int)sizeof path) {
        file = fopen(path,"rb");
        if (file) {
            art->brand_loaded = fread(art->brand,sizeof art->brand,1,file) == 1 && fgetc(file) == EOF && !ferror(file);
            fclose(file);
        }
    }
    return ok;
}

bool artwork_read_icon(const char *folder, const App *app, uint32_t *pixels) {
    char path[STORE_PATH];
    int length = snprintf(path, sizeof path, "%s/icons/%s.rgba", folder, app->package.id);
    if (length < 0 || (size_t)length >= sizeof path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fread(pixels, sizeof *pixels, ART_SIZE * ART_SIZE, file) == ART_SIZE * ART_SIZE &&
        fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return ok;
}

int artwork_slot(const Artwork *art, unsigned index) {
    for (unsigned i = 0; i < ART_CACHE_SIZE; ++i)
        if (art->used[i] && art->indices[i] == index) return (int)i;
    return -1;
}

void artwork_update(Artwork *art, const char *folder, const Store *store) {
    memset(art->used, 0, sizeof art->used);
    for (unsigned i = 0; i < store->count && i < ART_CACHE_SIZE; ++i) {
        art->used[i] = true; art->indices[i] = i;
        art->loaded[i] = artwork_read_icon(folder, &store->apps[i], art->icons[i]);
    }
}
