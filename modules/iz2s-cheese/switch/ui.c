#include "ui.h"
#include "github-mark.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { FILTER_ALL, FILTER_INSTALLED, FILTER_UPDATES, FILTER_TOOLS,
    FILTER_MEDIA, FILTER_EMULATORS, FILTER_GAMES, FILTERS };
enum { PAGE_SIZE = STORE_PAGE_SIZE, CARD_X = 272, CARD_Y = 142, CARD_H = 302, CARD_GAP = 14 };
enum { PAGE_Y = 585, PAGE_PREV_X = 1024, PAGE_NEXT_X = 1182, PAGE_BUTTON_W = 52, PAGE_BUTTON_H = 42 };
enum { SMALL, NORMAL, MEDIUM, LARGE };

static const char *const filters[FILTERS] = {
    "All apps", "Installed", "Updates", "Tools", "Media", "Emulators", "Games"
};
static const unsigned font_height[] = { 24, 28, 34, 42 };
static const uint32_t background = 0xff321832, panel = 0xff512f37, edge = 0xff815e71,
    bar = 0xff2c1931, sidebar = 0xff42294b, accent = 0xfff8e5a3,
    ink = 0xfffcf7fa, muted = 0xffe4d1d9, dim = 0xffc7afbf,
    success = 0xff9aedaa, warning = 0xff8bd5ff;

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

static int rail_width(const View *view) { return view->compact ? 82 : 222; }
static int catalog_x(const View *view) { return rail_width(view) + 30; }
static int card_width(const View *view) { return (1234-catalog_x(view)-(PAGE_SIZE-1)*CARD_GAP)/PAGE_SIZE; }

static unsigned glyph_index(unsigned char ch) { return ch >= 32 && ch <= 126 ? ch - 32 : '?' - 32; }

static int text_width(const Canvas *canvas, int size, const char *text) {
    int width = 0;
    for (const unsigned char *in = (const unsigned char *)text; *in; ++in)
        if ((*in & 0xc0) != 0x80) width += canvas->art->fonts[size].advance[glyph_index(*in)];
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
            uint32_t color = pixels[row*ART_SIZE/size*ART_SIZE + col*ART_SIZE/size];
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
    rounded(canvas,x+12,y+8,30,30,13,filled?background:edge,filled?background:edge);
    label(canvas,x+21,y+12,20,SMALL,accent,letter);
    label(canvas,x+53,y+7,width-61,NORMAL,filled?background:ink,caption);
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
    bool online = app->package.release_url[0];
    rounded(canvas,x+12,y+14,online?72:42,28,7,bar,bar);
    label(canvas,x+20,y+16,72,SMALL,online?accent:warning,online?"GitHub":"SD");
    rounded(canvas,x+width-58,y+14,46,28,7,bar,bar);
    label(canvas,x+width-52,y+16,39,SMALL,accent,kind(app));
    int size = width - 44;
    if (size > 154) size = 154;
    int cover_x = x+(width-size)/2;
    rounded(canvas,x+12,y+54,width-24,166,14,0xff35223e,0xff28162f);
    cover(canvas,cover_x,y+60+(154-size)/2,app->icon,size);
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
    label(canvas,CARD_X+22,356,178,NORMAL,muted,kind(app));
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

enum { SETTINGS_Y = 282, SETTINGS_STEP = 66, SETTINGS_W = 952, SETTINGS_H = 59, SETTINGS_COUNT = 5 };

static void settings(Canvas *canvas, const Store *store, const View *view) {
    label(canvas,CARD_X,48,800,LARGE,ink,"Settings");
    rectangle(canvas,CARD_X,105,952,1,edge);
    label(canvas,CARD_X,128,952,MEDIUM,ink,"Network");
    label(canvas,CARD_X,169,952,SMALL,muted,view->network[0] ? view->network : "Switch system connection");
    label(canvas,CARD_X,200,952,SMALL,muted,view->addresses);
    label(canvas,CARD_X,230,952,SMALL,dim,"Change Wi-Fi and DNS in HOME > System Settings > Internet.");
    const char *titles[SETTINGS_COUNT] = {view->settings.online ? "Catalog source: Online" : "Catalog source: SD card",
        "Catalog URL", view->settings.online ? "Download catalog" : "Reload SD catalog", "Test GitHub connection",
        "Cheese update"};
    const App *self = store_self_entry(store);
    const char *update_status = !self ? "Load a catalog with an IZ2S Cheese entry" :
        self->state == UPDATE_AVAILABLE ? "An official Cheese update is available" :
        self->installed_version[0] ? "No newer Cheese version in this catalog" :
        "Use the official release to update this copy";
    char list_status[90];
    if (store->remote_count)
        snprintf(list_status,sizeof list_status,"%u cached entries from the configured source",store->remote_count);
    else snprintf(list_status,sizeof list_status,"%s", view->settings.online ?
                  "Download catalog and thumbnails" : "Reload catalog.tsv and artwork.bin");
    const char *descriptions[SETTINGS_COUNT] = {
        view->settings.online ? "Use the configured URL and its cached files" : "sdmc:" STORE_CATALOG_DIR "/",
        view->settings.url[0] ? view->settings.url : "No URL configured",
        list_status,
        view->github[0] ? view->github : "Checks DNS, HTTPS and the GitHub release server",
        update_status,
    };
    for (unsigned i = 0; i < SETTINGS_COUNT; ++i) {
        int y = SETTINGS_Y + (int)i * SETTINGS_STEP;
        rectangle(canvas,CARD_X,y,SETTINGS_W,SETTINGS_H,panel);
        outline(canvas,CARD_X,y,SETTINGS_W,SETTINGS_H,view->settings_row == i ? 2 : 1,
            view->settings_row == i ? ink : edge);
        label(canvas,CARD_X+16,y+5,880,NORMAL,ink,titles[i]);
        label(canvas,CARD_X+16,y+33,918,SMALL,muted,descriptions[i]);
    }

}

static bool settings_screen(Screen screen) {
    return screen == SETTINGS || screen == CATALOG_INFO || screen == SETTINGS_RESULT;
}

static void dialog(Canvas *canvas, const App *app, const View *view) {
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
        const char *const links[] = {"https://github.com/byizmysh", "https://github.com/byizmysh/IZ2S-Platform"};
        const char *const titles[] = {"Author", "Project"};
        for (int i = 0; i < 2; ++i) {
            int y = 261 + i * 86;
            rounded(canvas,252,y,776,70,10,panel,bar);
            for (int row = 0; row < 24; ++row)
                for (int col = 0; col < 24; ++col) {
                    uint32_t *pixel = &canvas->pixels[(y+23+row)*canvas->stride+268+col];
                    *pixel = blend(*pixel,ink,github_mark[row][col]);
                }
            label(canvas,310,y+8,700,SMALL,muted,titles[i]);
            label(canvas,310,y+30,700,NORMAL,ink,links[i]);
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
    if (!view->busy && view->screen == CATALOG_INFO) {
        rounded(canvas,102,80,1076,532,16,panel,bar);
        label(canvas,132,105,1016,MEDIUM,ink,"Download catalog");
        label(canvas,132,155,1016,NORMAL,muted,"Publish catalog.tsv and artwork.bin in the same directory.");
        label(canvas,132,190,1016,NORMAL,muted,"Enter its raw GitHub HTTPS URL or private LAN HTTP URL in Catalog URL.");
        label(canvas,132,235,1016,SMALL,dim,"Download from");
        wrap(canvas,132,270,1016,8,SMALL,ink,view->settings.url[0] ? view->settings.url : "Set Catalog URL in Settings first.");
        label(canvas,132,490,1016,SMALL,muted,"The catalog and thumbnails are saved on SD for offline browsing.");
        if (view->settings.url[0]) action_button(canvas,660,549,235,"A","Download",true);
        action_button(canvas,920,549,225,"B","Back",false);
        return;
    }
    if (!view->busy && view->screen == SETTINGS_RESULT) {
        rounded(canvas,182,150,916,420,16,panel,bar);
        label(canvas,212,180,856,MEDIUM,ink,view->result_title);
        wrap(canvas,212,240,856,6,NORMAL,view->failure?warning:success,view->message);
        label(canvas,212,440,856,SMALL,muted,view->network);
        label(canvas,212,472,856,SMALL,muted,view->addresses);
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
    int rail = rail_width(view);
    rounded(&canvas,12,16,rail-20,625,18,sidebar,bar);
    if (art->brand_loaded)
        for (int row = 0; row < BRAND_SIZE; ++row)
            memcpy(&pixels[(33+row)*stride+22],art->brand[row],sizeof art->brand[row]);
    if (view->compact && !art->brand_loaded) label(&canvas,25,36,46,MEDIUM,accent,"IZ");
    if (!view->compact) {
        label(&canvas,76,38,138,NORMAL,ink,"IZ2S Cheese");
        label(&canvas,76,65,124,SMALL,dim,"Homebrew");
    }
    rounded(&canvas,24,110,rail-44,31,8,panel,panel);
    label(&canvas,34,113,rail-50,SMALL,accent,view->compact?">":"<   Collapse");
    const char *short_filters[] = {"All", "IN", "UP", "TL", "AV", "EM", "GM"};
    for (unsigned i = 0; i < FILTERS; ++i) {
        int y = 158 + (int)i*53;
        bool active = view->screen == CATALOG && view->filter == i;
        if (active) rounded(&canvas,22,y,rail-40,43,10,accent,accent);
        label(&canvas,31,y+8,rail-52,view->compact?SMALL:NORMAL,active?background:muted,
              view->compact?short_filters[i]:filters[i]);
    }
    if (!view->compact) label(&canvas,29,605,176,SMALL,dim,"by IZMYSH");
    const App *app = view->count ? &store->apps[view->visible[view->cursor]] : NULL;
    bool modal = view->screen == ABOUT || view->screen == REPOSITORY ||
        view->screen == CATALOG_INFO || view->screen == SETTINGS_RESULT ||
        (view->busy && !view->minimized) ||
        (app && (view->screen == CONFIRM_REMOVE || view->screen == CONFIRM_COMPATIBILITY));
    if (settings_screen(view->screen)) settings(&canvas,store,view);
    else if (app && view->screen != CATALOG) detail(&canvas,app,view);
    else {
        int left = catalog_x(view), width = card_width(view);
        label(&canvas,left,42,670,LARGE,ink,filters[view->filter]);
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
        } else if (!view->busy)
            label(&canvas,left,170,940,MEDIUM,muted,store->count ? "No applications in this view" :
                  "Open Settings to add a catalog.");
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
    if (!view->busy && view->message[0] && !settings_screen(view->screen))
        label(&canvas,CARD_X,619,952,SMALL,view->failure?warning:success,view->message);
    rectangle(&canvas,0,654,SCREEN_WIDTH,66,bar);
    rectangle(&canvas,0,654,SCREEN_WIDTH,1,edge);
    if (modal) {
        dialog(&canvas,app,view);
        return;
    }
    if (view->screen == CATALOG) {
        footer_key(&canvas,27,"L / R","Categories",false);
        footer_key(&canvas,264,"ZL","Sidebar",false);
        if (!view->busy) footer_key(&canvas,420,"X","About",false);
        footer_key(&canvas,570,"Y",view->busy?"Download":"Refresh",false);
        footer_key(&canvas,773,"A","Details",true);
        footer_key(&canvas,916,"-","Settings",false);
        footer_key(&canvas,1071,"B",view->busy?"Download":"Exit",false);
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

void view_filter(View *view, const Store *store) {
    view->count = 0;
    if (view->visible_capacity < store->count) {
        unsigned *next = malloc((size_t)store->count * sizeof *next);
        if (!next) { view->cursor = 0; view->failure = true; snprintf(view->message, sizeof view->message, "Not enough memory to display catalog."); return; }
        free(view->visible); view->visible = next; view->visible_capacity = store->count;
    }
    for (unsigned i = 0; i < store->count; ++i)
        if (selected_filter(view->filter, &store->apps[i])) view->visible[view->count++] = i;
    if (view->cursor >= view->count) view->cursor = view->count ? view->count-1 : 0;
    if (!view->count && !settings_screen(view->screen) && view->screen != ABOUT) view->screen = CATALOG;
}

static Command settings_action(View *view, const Store *store) {
    if (view->settings_row == 4) {
        const App *self = store_self_entry(store);
        if (self && self->state == UPDATE_AVAILABLE) return UPDATE_STORE;
        view->screen = SETTINGS_RESULT;
        view->failure = false;
        snprintf(view->result_title, sizeof view->result_title, "Cheese update");
        snprintf(view->message, sizeof view->message, "%s",
            !self ? "Load a catalog that contains an IZ2S Cheese entry." :
            self->installed_version[0] ? "This catalog has no newer Store version." :
            "Use the official release to update a nonstandard Cheese path.");
        return NO_JOB;
    }
    if (view->settings_row == 0) return TOGGLE_CATALOG_SOURCE;
    if (view->settings_row == 2) {
        if (!view->settings.online) { view->screen = CATALOG; return REFRESH; }
        view->screen = CATALOG_INFO;
        return NO_JOB;
    }
    return view->settings_row == 1 ? EDIT_URL : CHECK_NETWORK;
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
        buttons &= ~BTN_X;
        if (view->screen == DETAILS || view->screen == SETTINGS) buttons &= ~BTN_A;
    }
    if (!buttons) return NO_JOB;
    if (view->screen == ABOUT) {
        if (buttons & (BTN_B | BTN_X | BTN_PLUS)) view->screen = CATALOG;
        return NO_JOB;
    }
    if (view->screen == REPOSITORY) {
        if (buttons & (BTN_B | BTN_R | BTN_PLUS)) view->screen = DETAILS;
        return NO_JOB;
    }
    if (view->screen == CATALOG_INFO || view->screen == SETTINGS_RESULT) {
        if (buttons & (BTN_B | BTN_PLUS)) view->screen = SETTINGS;
        else if (buttons & BTN_A) {
            if (view->screen == CATALOG_INFO && view->settings.url[0]) return FETCH_LIST;
            if (view->screen == SETTINGS_RESULT) view->screen = SETTINGS;
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
    if (buttons & BTN_ZL) { view->compact = !view->compact; return NO_JOB; }
    if (view->screen == SETTINGS) {
        if (buttons & (BTN_B | BTN_MINUS)) view->screen = CATALOG;
        else if ((buttons & BTN_UP) && view->settings_row) --view->settings_row;
        else if ((buttons & BTN_DOWN) && view->settings_row+1 < SETTINGS_COUNT) ++view->settings_row;
        else if (buttons & BTN_A) return settings_action(view, store);
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
    if (buttons & BTN_B) return EXIT;
    if (buttons & BTN_MINUS) { view->screen = SETTINGS; return NO_JOB; }
    if (buttons & BTN_X) { view->screen = ABOUT; return NO_JOB; }
    if (buttons & BTN_Y) return REFRESH;
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
    if (view->screen == CATALOG_INFO) {
        if (hit(x,y,660,549,235,46)) return view_input(view,store,BTN_A);
        if (hit(x,y,920,549,225,46)) return view_input(view,store,BTN_B);
        return NO_JOB;
    }
    if (view->screen == SETTINGS_RESULT) {
        if (hit(x,y,842,510,225,46)) return view_input(view,store,BTN_B);
        return NO_JOB;
    }
    if (hit(x,y,24,110,rail_width(view)-44,31)) return view_input(view,store,BTN_ZL);
    if (hit(x,y,1062,654,218,66)) return view_input(view,store,BTN_B);
    if (view->screen == SETTINGS) {
        if (hit(x,y,813,654,190,66)) return view_input(view,store,BTN_A);
        for (unsigned i = 0; i < SETTINGS_COUNT; ++i)
            if (hit(x,y,CARD_X,SETTINGS_Y+(int)i*SETTINGS_STEP,SETTINGS_W,SETTINGS_H)) {
                view->settings_row = i;
                return view->busy ? NO_JOB : settings_action(view, store);
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
    if (hit(x,y,901,654,156,66)) return view_input(view,store,BTN_MINUS);
    if (hit(x,y,260,654,145,66)) return view_input(view,store,BTN_ZL);
    if (hit(x,y,420,654,140,66)) return view_input(view,store,BTN_X);
    if (hit(x,y,768,654,126,66)) return view_input(view,store,BTN_A);
    if (hit(x,y,24,654,195,66)) return view_input(view,store,BTN_R);
    if (hit(x,y,14,158,rail_width(view)-22,FILTERS*53)) {
        view->filter = (unsigned)(y-158)/53;
        view->cursor = 0;
        view_filter(view,store);
    }
    if (hit(x,y,560,654,195,66)) return view_input(view,store,BTN_Y);
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
    int width = card_width(view);
    for (unsigned i = 0; i < PAGE_SIZE && start+i < view->count; ++i)
        if (hit(x,y,catalog_x(view)+(int)i*(width+CARD_GAP),CARD_Y,width,CARD_H)) {
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
