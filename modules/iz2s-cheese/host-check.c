#include "switch/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Store store;
static Artwork artwork;
static uint32_t frame[SCREEN_WIDTH * SCREEN_HEIGHT];

static int picture(const char *directory, const char *name, View *view) {
    if (!artwork_open(&artwork, directory)) return 1;
    artwork_update(&artwork, directory, &store);
    view_draw(frame, SCREEN_WIDTH, &artwork, &store, view);
    FILE *file = fopen(name, "wb");
    if (!file) return 1;
    fprintf(file,"P6\n%d %d\n255\n",SCREEN_WIDTH,SCREEN_HEIGHT);
    for (unsigned i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        uint8_t rgb[] = {frame[i], frame[i] >> 8, frame[i] >> 16};
        if (fwrite(rgb,1,3,file) != 3) { fclose(file); return 1; }
    }
    return fclose(file) != 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *command = argv[2];
    char message[STORE_MESSAGE];
    if (!store_load(&store, argv[1], message)) {
        fprintf(stderr,"%s\n",message);
        return 1;
    }
    View view = {0};
    store_settings_read(store.root, &view.settings);
    if (!strcmp(command, "refresh") || !strcmp(command, "load-catalog")) {
        if (!strcmp(command, "refresh") && view_input(&view, &store, BTN_Y) != REFRESH) return 3;
        bool ok = store_refresh_catalog(&store, argv[1], &view.settings,
                                         !strcmp(command, "refresh"), message);
        puts(message);
        return ok ? 0 : 1;
    }
    if (!strcmp(command, "artwork")) {
        printf("%u\n", store_read_artwork(&store, &view.settings, NULL));
        return 0;
    }
    const char *art = getenv("IZ_STORE_TEST_ARTWORK");
    for (unsigned i = 0; i < store.count; ++i) {
        store_refresh(&store, &store.apps[i]);
        if (art) store_sync_icon(&store, &store.apps[i], art);
    }
    view_filter(&view, &store);
    if (!strcmp(command,"fetch-list")) {
        if (argc != 4 || strlen(argv[3]) >= sizeof view.settings.url) return 2;
        strcpy(view.settings.url, argv[3]);
        view.settings.online = view.settings.url[0] != '\0';
        bool ok = store_settings_write(store.root, &view.settings, message) &&
                  store_fetch_list(&store, &view.settings, message);
        puts(message);
        return ok ? 0 : 1;
    }
    if (!strcmp(command,"probe")) {
        if (argc != 4) return 2;
        bool ok = github_probe(argv[3], message);
        puts(ok ? "GitHub and its release CDN responded over verified HTTPS." : message);
        return ok ? 0 : 1;
    }
    if (!strcmp(command,"sources")) {
        for (unsigned i = 0; i < store.count; ++i) {
            char url[RELEASE_URL_MAX];
            store_repository(&store.apps[i].package,url);
            printf("%s\t%s\n",store.apps[i].package.id,url);
        }
        return 0;
    }
    if (!strcmp(command,"list")) {
        for (unsigned i = 0; i < store.count; ++i) {
            const App *app = &store.apps[i];
            printf("%s\t%u\t%s\t%s\n",app->package.id,app->state,app->installed_version,app->package.version);
        }
        return 0;
    }
    if (!strcmp(command, "installed")) {
        view_input(&view, &store, BTN_R);
        for (unsigned i = 0; i < view.count; ++i)
            puts(store.apps[view.visible[i]].package.id);
        return 0;
    }
    if (!strcmp(command, "icon") && argc == 4) {
        static uint32_t pixels[STORE_ICON_SIZE * STORE_ICON_SIZE];
        for (unsigned i = 0; i < store.count; ++i)
            if (!strcmp(store.apps[i].package.id, argv[3]) &&
                store_read_installed_icon(&store, &store.apps[i], pixels)) {
                printf("%08x\n", pixels[0]);
                return 0;
            }
        return 1;
    }
    if (argc < 4) return 2;
    bool selected = !strcmp(argv[3],"-");
    if (!strcmp(command,"picture") && !strcmp(argv[3],"compact")) {
        view_input(&view, &store, BTN_ZL);
        selected = view.compact;
    }
    if (!strcmp(command,"picture") && !strcmp(argv[3],"settings")) {
        view_input(&view, &store, BTN_MINUS);
        selected = view.screen == SETTINGS;
    }
    if (!strcmp(command,"picture") && !strcmp(argv[3],"about")) {
        view_input(&view, &store, BTN_X);
        selected = view.screen == ABOUT;
    }
    for (unsigned i = 0; i < store.count; ++i)
        if (!strcmp(store.apps[i].package.id,argv[3])) {
            view.cursor = i;
            view_input(&view,&store,BTN_A);
            selected = true;
            break;
        }
    if (!selected) return 2;
    if (!strcmp(command,"picture")) return argc == 6 ? picture(argv[4],argv[5],&view) : 2;
    const App *app = &store.apps[view.visible[view.cursor]];
    bool result;
    if (!strcmp(command,"install") || !strcmp(command,"raw-install")) {
        if (strcmp(command,"raw-install")) {
            Command action = view_input(&view,&store,BTN_A);
            if (app->package.kind == PACKAGE_IZM) {
                if (action != NO_JOB || view.screen != CONFIRM_COMPATIBILITY ||
                    view_input(&view,&store,BTN_B) != NO_JOB || view.screen != DETAILS) return 3;
                view_input(&view,&store,BTN_A);
                action = view_input(&view,&store,BTN_A);
            }
            if (action != INSTALL) return 3;
        }
        result = store_install(&store,app,(Progress){0},message);
    } else if (!strcmp(command,"remove") || !strcmp(command,"raw-remove")) {
        if (strcmp(command,"raw-remove")) {
            view_input(&view,&store,BTN_X);
            if (view.screen != CONFIRM_REMOVE) return 3;
            view_input(&view,&store,BTN_B);
            if (view.screen != DETAILS) return 3;
            view_input(&view,&store,BTN_X);
            if (view_input(&view,&store,BTN_A) != REMOVE) return 3;
        }
        result = store_remove(&store,app,message);
    } else return 2;
    if (result && art) {
        App changed = *app;
        store_refresh(&store, &changed);
        store_sync_icon(&store, &changed, art);
    }
    puts(message);
    return result ? 0 : 1;
}
