#include "ui.h"
#include <switch.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#ifdef IZ_STORE_MODULE
#include <sb4/native-app.h>
#endif

#define SD_ROOT "sdmc:"
#define CHECK_REQUEST SD_ROOT STORE_DIR "/network-check.request"
#define CHECK_REPORT SD_ROOT STORE_DIR "/network-check.txt"

typedef struct {
    const Store *current;
    Store *result;
    const App *app;
    Command command;
    atomic_bool done, cancel;
    atomic_uint percent;
    bool ok, result_ready;
    char message[STORE_MESSAGE];
    char network[STORE_MESSAGE], addresses[STORE_MESSAGE], github[STORE_MESSAGE];
    StoreSettings settings;
} Task;

static Store catalogs[2];
static Artwork artwork;

static void network_status(Task *task) {
    task->addresses[0] = '\0';
    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) {
        snprintf(task->network, sizeof task->network, "Switch network status unavailable (0x%08x).", rc);
        return;
    }
    NifmInternetConnectionType type;
    NifmInternetConnectionStatus state;
    u32 strength, ip, mask, gateway, dns, secondary_dns;
    rc = nifmGetInternetConnectionStatus(&type, &strength, &state);
    if (R_FAILED(rc)) snprintf(task->network, sizeof task->network, "No active Switch connection (0x%08x).", rc);
    else if (type == NifmInternetConnectionType_WiFi)
        snprintf(task->network, sizeof task->network, "Wi-Fi, signal %u/3 | Switch internet %s", strength,
            state == NifmInternetConnectionStatus_Connected ? "connected" : "pending");
    else
        snprintf(task->network, sizeof task->network, "%s | Switch internet %s",
            type == NifmInternetConnectionType_Ethernet ? "Ethernet" : "System network",
            state == NifmInternetConnectionStatus_Connected ? "connected" : "pending");
    if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&ip, &mask, &gateway, &dns, &secondary_dns))) {
        char ip_text[16], gateway_text[16], dns_text[16];
        if (inet_ntop(AF_INET, &ip, ip_text, sizeof ip_text) &&
            inet_ntop(AF_INET, &gateway, gateway_text, sizeof gateway_text) &&
            inet_ntop(AF_INET, &dns, dns_text, sizeof dns_text))
            snprintf(task->addresses, sizeof task->addresses, "IP %s   Gateway %s   DNS %s",
                ip_text, gateway_text, dns_text);
    }
    nifmExit();
}

static bool check_github(Task *task) {
    const char *url = NULL;
    for (unsigned i = 0; i < task->current->count; ++i) {
        const char *release = task->current->apps[i].package.release_url;
        if (*release) { url = release; break; }
    }
    bool ok = github_probe(url, task->github);
    if (ok) snprintf(task->github, sizeof task->github, "%s", url ?
        "GitHub and its release CDN responded over verified HTTPS." : "GitHub responded over verified HTTPS.");
    return ok;
}

static void write_report(const Task *task, const char *catalog) {
    FILE *file = fopen(CHECK_REPORT, "w");
    if (!file) return;
    bool ok = fprintf(file, "IZ2S Cheese network check\nNetwork: %s\n%s\nGitHub: %s\nEnabled online catalogs: %u\nCatalog: %s\n",
        task->network, task->addresses, task->github, store_enabled_online_catalogs(&task->settings), catalog) > 0;
    if (fclose(file)) ok = false;
    if (ok && R_SUCCEEDED(fsdevCommitDevice("sdmc")) && task->command == STARTUP_CHECK) {
        unlink(CHECK_REQUEST);
        fsdevCommitDevice("sdmc");
    }
}

static void refresh_apps(Store *store) {
    for (unsigned i = 0; i < store->count; ++i) store_refresh(store, &store->apps[i]);
}

static void progress(void *context, uint64_t done, uint64_t total) {
    Task *task = context;
    task->result_ready = false;
    if (total) atomic_store(&task->percent, (unsigned)(done * 99 / total));
}

static void run_task(void *context) {
    Task *task = context;
    task->result_ready = false;
    if (task->command == REFRESH || task->command == LOAD_CATALOG || task->command == OPEN_CATALOGS) {
        CatalogRefreshMode mode = CATALOG_CACHED;
        if (task->command == REFRESH) mode = CATALOG_REFRESH;
        if (task->command == OPEN_CATALOGS) mode = CATALOG_CHECK;
        task->ok = store_refresh_catalog(task->result, SD_ROOT, &task->settings, mode, task->message);
        task->result_ready = true;
        refresh_apps(task->result);
        network_status(task);
    } else if (task->command == FETCH_LIST || task->command == STARTUP_CHECK || task->command == CHECK_NETWORK) {
        network_status(task);
        if (task->command == CHECK_NETWORK || task->command == STARTUP_CHECK) {
            bool online = check_github(task);
            if (task->command == CHECK_NETWORK) {
                task->ok = online;
                snprintf(task->message, sizeof task->message, "%s", task->github);
                write_report(task, "Catalog was not requested.");
            }
        }
        if (task->command == FETCH_LIST || task->command == STARTUP_CHECK) {
            if (!store_clone(task->result, task->current)) {
                task->ok = false; snprintf(task->message, sizeof task->message, "Not enough memory for catalog.");
                atomic_store(&task->done, true); return;
            }
            task->result_ready = true;
            snprintf(task->result->root, sizeof task->result->root, "%s", SD_ROOT);
            task->ok = store_fetch_list(task->result, &task->settings, task->message);
            refresh_apps(task->result);
            if (task->command == STARTUP_CHECK) write_report(task, task->message);
        }
    } else if (task->command == CHECK_STORE_UPDATE) {
        task->ok = store_clone(task->result, task->current);
        if (task->ok) task->ok = store_check_update(task->result, task->message);
        else snprintf(task->message, sizeof task->message, "Not enough memory to check updates.");
        task->result_ready = task->ok;
        network_status(task);
    } else {
        if (task->command == INSTALL || task->command == UPDATE_STORE)
            task->ok = store_install(task->current, task->app, (Progress){.report = progress, .context = task, .cancel = &task->cancel}, task->message);
        else task->ok = store_remove(task->current, task->app, task->message);
        if (!store_clone(task->result, task->current)) {
                task->ok = false; snprintf(task->message, sizeof task->message, "Not enough memory for catalog.");
                atomic_store(&task->done, true); return;
            }
        task->result_ready = true;
        if (task->command == UPDATE_STORE && task->current->update_checked)
            store_refresh(task->result, &task->result->update);
        else {
            unsigned index = (unsigned)(task->app - task->current->apps);
            store_refresh(task->result, &task->result->apps[index]);
        }
    }
    atomic_store(&task->done, true);
}

static bool start_task(Task *task, Thread *thread, View *view, const Store *store, Store *next, Command command) {
    if (view->busy) return false;
    view->minimized = false;
    task->current = store;
    task->result = next;
    task->command = command;
    task->settings = view->settings;
    task->app = command == UPDATE_STORE ? store_self_entry(store) :
        view->count ? &store->apps[view->visible[view->cursor]] : NULL;
    atomic_store(&task->done, false);
    atomic_store(&task->cancel, false);
    view->cancellable = (command == INSTALL || command == UPDATE_STORE) && task->app && task->app->package.release_url[0];
    snprintf(view->source, sizeof view->source, "%s", view->cancellable ? task->app->package.release_url : "");
    snprintf(view->download_title, sizeof view->download_title, "%s", view->cancellable ? task->app->package.title : "");
    atomic_store(&task->percent, 0);
    Result result = threadCreate(thread, run_task, task, NULL, 0x10000, 0x32, -2);
    if (R_SUCCEEDED(result)) {
        result = threadStart(thread);
        if (R_FAILED(result)) threadClose(thread);
    }
    view->busy = R_SUCCEEDED(result);
    view->failure = R_FAILED(result);
    view->percent = 0;
    if (view->failure) snprintf(view->message, sizeof view->message, "Could not start SD operation (0x%08x).", result);
    else snprintf(view->message, sizeof view->message, "%s",
        command == REFRESH && store_enabled_online_catalogs(&view->settings) ? "Updating catalogs and thumbnails..." :
        command == OPEN_CATALOGS && store_enabled_online_catalogs(&view->settings) ? "Checking catalogs for updates..." :
        command == REFRESH || command == LOAD_CATALOG || command == OPEN_CATALOGS ? "Loading catalog..." :
        command == REMOVE ? "Removing the application..." :
        command == FETCH_LIST ? "Fetching catalogs and thumbnails..." :
        command == CHECK_STORE_UPDATE ? "Checking official Cheese release..." :
        command == CHECK_NETWORK || command == STARTUP_CHECK ? "Checking the network..." :
        task->app && task->app->package.release_url[0] ? "Downloading" :
        "Copying and verifying files...");
    return view->busy;
}

static unsigned buttons(u64 keys) {
    const u64 platform[] = {
        HidNpadButton_AnyUp, HidNpadButton_AnyDown, HidNpadButton_AnyLeft, HidNpadButton_AnyRight,
        HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
        HidNpadButton_L, HidNpadButton_R, HidNpadButton_Plus, HidNpadButton_Minus,
        HidNpadButton_ZL, HidNpadButton_ZR,
    };
    unsigned result = 0;
    for (unsigned i = 0; i < sizeof platform / sizeof platform[0]; ++i)
        if (keys & platform[i]) result |= 1u << i;
    return result;
}

static bool apply_catalog_settings(View *view, const StoreSettings *settings) {
    view->failure = !store_settings_write(SD_ROOT, settings, view->message);
    if (view->failure) return false;
    view->settings = *settings;
    return true;
}

static bool catalog_error(View *view, const char *text) {
    view->failure = true;
    snprintf(view->message, sizeof view->message, "%s", text);
    return false;
}

static bool read_keyboard(View *view, const char *guide, const char *initial,
                          unsigned max, char *input, size_t capacity) {
    SwkbdConfig keyboard;
    Result rc = swkbdCreate(&keyboard, 0);
    if (R_FAILED(rc)) {
        view->failure = true;
        snprintf(view->message, sizeof view->message, "Could not open the keyboard (0x%08x).", rc);
        return false;
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetType(&keyboard, SwkbdType_QWERTY);
    swkbdConfigSetGuideText(&keyboard, guide);
    swkbdConfigSetStringLenMax(&keyboard, max);
    swkbdConfigSetInitialText(&keyboard, initial);
    rc = swkbdShow(&keyboard, input, capacity);
    swkbdClose(&keyboard);
    return R_SUCCEEDED(rc);
}

static bool change_catalog_url(View *view, StoreSettings *settings, unsigned index) {
    bool adding = index == settings->count;
    if (adding && settings->count == STORE_MAX_CATALOGS)
        return catalog_error(view, "Remove a saved catalog before adding another.");
    char input[RELEASE_URL_MAX], url[RELEASE_URL_MAX];
    if (!read_keyboard(view, "Catalog URL: raw GitHub or local HTTP",
                       adding ? "" : settings->sources[index].url, 500, input, sizeof input)) return false;
    if (!store_catalog_address(input, url) || !url[0])
        return catalog_error(view, "Use a GitHub catalog address or a local HTTP catalog URL.");
    for (unsigned i = 0; i < settings->count; ++i)
        if (i != index && !strcmp(settings->sources[i].url, url))
            return catalog_error(view, "This catalog is already saved.");
    strcpy(settings->sources[index].url, url);
    if (adding) {
        settings->sources[index].enabled = true;
        ++settings->count;
    }
    return true;
}

static bool configure_catalog(View *view, Command command) {
    StoreSettings settings = view->settings;
    unsigned row = view->catalog_row;
    if (command == ADD_CATALOG) {
        row = settings.count;
        if (!change_catalog_url(view, &settings, row)) return false;
    } else if (command == ENABLE_COMMUNITY) {
        row = store_community_catalog(&settings);
        if (row == settings.count) return false;
        settings.sources[row].enabled = true;
    } else if (command == TOGGLE_CATALOG && row == settings.count) {
        settings.sd_enabled = !settings.sd_enabled;
    } else {
        if (row >= settings.count) return false;
        CatalogSource *source = &settings.sources[row];
        if (command == TOGGLE_CATALOG) {
            source->enabled = !source->enabled;
        } else if (command == EDIT_CATALOG) {
            if (!change_catalog_url(view, &settings, row)) return false;
        } else {
            --settings.count;
            for (unsigned i = row; i < settings.count; ++i) settings.sources[i] = settings.sources[i + 1];
            if (row && row == settings.count) --row;
        }
    }
    if (!apply_catalog_settings(view, &settings)) return false;
    view->catalog_row = row;
    return true;
}

typedef struct {
    View view;
    Task task;
    Thread thread;
    unsigned active;
    bool touched, check_on_start, dirty;
    const unsigned char *art_records;
    unsigned art_count;
    unsigned cache_next;
    uint64_t held;
} Session;

static void reset_artwork(Session *session, const Store *store) {
    (void)store;
    memset(artwork.used, 0, sizeof artwork.used);
    session->cache_next = 0;
}

static bool embedded_icon(const Session *session, const App *app, uint32_t *pixels) {
    const size_t record_size = 48 + sizeof artwork.icons[0];
    for (unsigned i = 0; i < session->art_count; ++i) {
        const unsigned char *record = session->art_records + record_size * i;
        if (strncmp((const char *)record, app->package.id, 48)) continue;
        memcpy(pixels, record + 48, sizeof artwork.icons[0]);
        return true;
    }
    return false;
}

static void load_artwork(Session *session, const Store *store) {
    if (session->view.busy && !session->view.minimized) return;
    const View *view = &session->view;
    unsigned first = view->cursor / STORE_PAGE_SIZE * STORE_PAGE_SIZE;
    unsigned end = first + STORE_PAGE_SIZE;
    if (end > view->count) end = view->count;
    if (view->screen != CATALOG) { first = view->cursor; end = view->count ? first + 1 : 0; }
    for (unsigned item = first; item < end; ++item) {
        unsigned index = view->visible[item];
        if (artwork_slot(&artwork, index) >= 0) continue;
        unsigned slot = session->cache_next++ % ART_CACHE_SIZE;
        /* Keep every image on the current page while replacing older pages. */
        for (unsigned n = 0; n < ART_CACHE_SIZE; ++n) {
            bool visible = false;
            for (unsigned j = first; j < end; ++j)
                if (artwork.used[slot] && artwork.indices[slot] == view->visible[j]) visible = true;
            if (!visible) break;
            slot = session->cache_next++ % ART_CACHE_SIZE;
        }
        const App *app = &store->apps[index];
        artwork.used[slot] = true; artwork.indices[slot] = index;
        char folder[STORE_PATH];
        int length = snprintf(folder, sizeof folder, "%s" STORE_DIR, store->root);
        artwork.loaded[slot] = store_read_artwork_icon(store, index, artwork.icons[slot]) ||
            embedded_icon(session, app, artwork.icons[slot]) ||
            (length >= 0 && (size_t)length < sizeof folder && artwork_read_icon(folder, app, artwork.icons[slot])) ||
            store_read_installed_icon(store, app, artwork.icons[slot]);
        if (artwork.loaded[slot] && !view->busy) store_sync_icon_pixels(store, app, artwork.icons[slot]);
        session->dirty = true;
    }
}

static void open_session(Session *session, bool hosted) {
    for (unsigned i = 0; i < 2; ++i) store_clear(&catalogs[i]);
    session->view.inside_iz2s = hosted;
    session->check_on_start = access(CHECK_REQUEST, F_OK) == 0;
    session->dirty = true;
    store_settings_read(SD_ROOT, &session->view.settings);
    start_task(&session->task, &session->thread, &session->view, &catalogs[0], &catalogs[1], OPEN_CATALOGS);
}

static bool tick_session(Session *session, unsigned pressed, bool touching, int x, int y) {
    View *view = &session->view;
    Task *task = &session->task;
    Store *store = &catalogs[session->active];
    if (view->busy && atomic_load(&task->done)) {
        threadWaitForExit(&session->thread);
        threadClose(&session->thread);
        unsigned selected = view->count ? view->visible[view->cursor] : UINT_MAX;
        bool app_action = task->command == INSTALL || task->command == UPDATE_STORE || task->command == REMOVE;
        bool changed = task->result_ready && task->command != CHECK_NETWORK;
        if (changed) session->active ^= 1u;
        store = &catalogs[session->active];
        if (changed && !app_action) reset_artwork(session, store);
        if (changed && app_action && task->command != UPDATE_STORE) {
            int slot = artwork_slot(&artwork, (unsigned)(task->app - task->current->apps));
            if (slot >= 0) artwork.used[slot] = false;
        }
        view->busy = false;
        view->failure = !task->ok && !atomic_load(&task->cancel);
        view->cancellable = false;
        view->minimized = false;
        snprintf(view->message, sizeof view->message, "%s", task->message);
        snprintf(view->network, sizeof view->network, "%s", task->network);
        snprintf(view->addresses, sizeof view->addresses, "%s", task->addresses);
        snprintf(view->github, sizeof view->github, "%s", task->github);
        if (task->command == REFRESH || task->command == LOAD_CATALOG || task->command == OPEN_CATALOGS ||
            task->command == FETCH_LIST || task->command == STARTUP_CHECK) {
            view->settings = task->settings;
            if (view->catalog_row > view->settings.count)
                view->catalog_row = view->settings.count;
            view->cursor = 0;
        }
        view_filter(view, store);
        if (app_action && (view->screen == DETAILS || view->screen == REPOSITORY)) {
            unsigned cursor = 0;
            while (cursor < view->count && view->visible[cursor] != selected) ++cursor;
            if (cursor < view->count) view->cursor = cursor;
            else view->screen = CATALOG;
        }
        if (task->command == CHECK_NETWORK || task->command == FETCH_LIST || task->command == CHECK_STORE_UPDATE ||
            task->command == STARTUP_CHECK || task->command == UPDATE_STORE) {
            view->screen = SETTINGS_RESULT;
            view->update_result = task->command == CHECK_STORE_UPDATE && task->ok;
            snprintf(view->result_title, sizeof view->result_title, "%s",
                task->command == CHECK_NETWORK ? "GitHub connection test" :
                task->command == UPDATE_STORE || task->command == CHECK_STORE_UPDATE ? "Cheese update" : "Catalog refresh");
        }
        if (session->check_on_start && task->command == OPEN_CATALOGS) {
            session->check_on_start = false;
            view->screen = SETTINGS;
            start_task(task, &session->thread, view, store, &catalogs[session->active ^ 1u], STARTUP_CHECK);
        }
        session->dirty = true;
    }
    unsigned percent = atomic_load(&task->percent);
    if (view->busy && percent != view->percent) { view->percent = percent; session->dirty = true; }
    Command command = view_input(view, store, pressed);
    if (touching && !session->touched && !pressed) command = view_touch(view, store, x, y);
    if (command == CANCEL_DOWNLOAD) {
        atomic_store(&task->cancel, true);
        view->cancellable = false;
        snprintf(view->message, sizeof view->message, "Cancelling...");
        session->dirty = true;
        command = NO_JOB;
    }
    if (command == EXIT) return false;
    if (command == EDIT_SEARCH) {
        char input[SEARCH_BYTES] = {0};
        if (read_keyboard(view, "App name, publisher or ID", view->search,
                          sizeof input - 1, input, sizeof input))
            view_search(view, store, input);
        command = NO_JOB;
    }
    if (command == ADD_CATALOG || command == EDIT_CATALOG || command == TOGGLE_CATALOG ||
        command == REMOVE_CATALOG || command == ENABLE_COMMUNITY)
        command = configure_catalog(view, command) ? LOAD_CATALOG : NO_JOB;
    if (command != NO_JOB && !view->busy)
        start_task(task, &session->thread, view, store, &catalogs[session->active ^ 1u], command);
    if (pressed || (touching && !session->touched) || command != NO_JOB) session->dirty = true;
    session->touched = touching;
    load_artwork(session, store);
    return true;
}

static bool step(Session *session, unsigned pressed) {
    HidTouchScreenState touch = {0};
    bool touching = hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0;
    return tick_session(session, pressed, touching,
        touching ? (int)touch.touches[0].x : 0, touching ? (int)touch.touches[0].y : 0);
}

static void close_session(Session *session) {
    if (session->view.busy) {
        threadWaitForExit(&session->thread);
        threadClose(&session->thread);
        session->view.busy = false;
    }
    for (unsigned i = 0; i < 2; ++i) store_clear(&catalogs[i]);
    free(session->view.visible); session->view.visible = NULL; session->view.visible_capacity = 0;
}

#ifdef IZ_STORE_MODULE
enum { STORE_FRAME_OFFSET = 0x200000 };
static IzNativeApp *host_owner;
static Session hosted_session;

static bool hosted_artwork(Session *session, const IzNativeApp *app) {
    const unsigned char *start = (const unsigned char *)app->controls + 1024;
    if (memcmp(start,"IZSTOREA",8)) return false;
    session->art_count = start[8];
    const unsigned char *fonts = start + 12;
    const unsigned char *brand = fonts + sizeof artwork.fonts;
    const unsigned char *records = brand + sizeof artwork.brand;
    if (12 + sizeof artwork.fonts + sizeof artwork.brand +
        session->art_count * (48 + sizeof artwork.icons[0]) > STORE_FRAME_OFFSET - 1024) return false;
    memcpy(artwork.fonts, fonts, sizeof artwork.fonts);
    memcpy(artwork.brand, brand, sizeof artwork.brand);
    artwork.brand_loaded = true;
    session->art_records = records;
    return true;
}

uint32_t iz_store_iza_event(IzNativeApp *app, uint32_t event) {
    Session *session = &hosted_session;
    if (event == IZ_NATIVE_INIT) {
        if (host_owner || !app || !app->controls || !app->buttons || !app->request_exit ||
            app->controls_bytes < STORE_FRAME_OFFSET + SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t)) return 1;
        memset(session, 0, sizeof *session);
        if (!hosted_artwork(session, app)) return 1;
        store_set_running_iza(IZ_STORE_VERSION);
        host_owner = app;
        session->held = app->buttons(app);
        open_session(session, true);
    } else if (app != host_owner || !app) return 0;
    else if (event == IZ_NATIVE_STOP) {
        if (session->view.busy && !atomic_load(&session->task.done)) {
            atomic_store(&session->task.cancel, true);
            return IZ_NATIVE_STOP_PENDING;
        }
        close_session(session);
        host_owner = NULL;
        return 0;
    } else if (event == IZ_NATIVE_FRAME) {
        u64 held = app->buttons(app);
        unsigned pressed = buttons(held & ~session->held);
        session->held = held;
        if (!step(session, pressed)) app->request_exit(app);
    }
    if (session->dirty) {
        uint32_t *pixels = (void *)((unsigned char *)app->controls + STORE_FRAME_OFFSET);
        view_draw(pixels, SCREEN_WIDTH, &artwork, &catalogs[session->active], &session->view);
        session->dirty = false;
    }
    return 0;
}
#else
int main(int argc, char **argv) {
    store_set_running_nro(argc > 0 ? argv[0] : NULL, IZ_STORE_VERSION);
    bool mounted = !fsdevGetDeviceFileSystem("sdmc");
    if (mounted && R_FAILED(fsdevMountSdmc())) return 1;
    Result result = romfsInit();
    bool have_romfs = R_SUCCEEDED(result);
    Framebuffer framebuffer = {0};
    bool have_framebuffer = false;
    if (have_romfs && artwork_open(&artwork, "romfs:")) {
        result = framebufferCreate(&framebuffer, nwindowGetDefault(), SCREEN_WIDTH, SCREEN_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2);
        have_framebuffer = R_SUCCEEDED(result);
        if (have_framebuffer) result = framebufferMakeLinear(&framebuffer);
    } else result = 1;
    if (R_SUCCEEDED(result)) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);
        hidInitializeTouchScreen();
        Session session = {0};
        open_session(&session, false);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (!step(&session, buttons(padGetButtonsDown(&pad)))) break;
            if (session.dirty) {
                uint32_t stride;
                uint32_t *pixels = framebufferBegin(&framebuffer, &stride);
                if (!pixels) { result = 1; break; }
                view_draw(pixels, stride / sizeof *pixels, &artwork, &catalogs[session.active], &session.view);
                framebufferEnd(&framebuffer);
                session.dirty = false;
            }
            svcSleepThread(12000000);
        }
        close_session(&session);
    }
    if (have_framebuffer) framebufferClose(&framebuffer);
    if (have_romfs) romfsExit();
    if (mounted) fsdevUnmountDevice("sdmc");
    return R_FAILED(result) ? 1 : 0;
}
#endif
