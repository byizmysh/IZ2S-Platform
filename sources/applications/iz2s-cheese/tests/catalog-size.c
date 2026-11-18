#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 3);
    unsigned count = (unsigned)strtoul(argv[2], NULL, 10);
    Store store = {0}, copy = {0}; View view = {0}; char message[STORE_MESSAGE];
    StoreSettings settings = {0};
    uint32_t pixels[STORE_ICON_SIZE * STORE_ICON_SIZE];
    assert(sizeof(Artwork) < 2 * 1024 * 1024);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        assert(store_load(&store, argv[1], message) && store.count == count);
        view_filter(&view, &store); assert(view.count == count);
        view.screen = CATALOG; view.cursor = count - 1;
        assert(view_input(&view, &store, BTN_A) == NO_JOB && view.screen == DETAILS);
        assert(view.visible[view.cursor] == count - 1);
        assert(store_clone(&copy, &store) && copy.apps != store.apps && copy.count == count);
        copy.apps[count - 1].state = INSTALLED;
        assert(store.apps[count - 1].state != INSTALLED);
        assert(store_read_artwork(&store, &settings, NULL) == count);
        assert(store_read_artwork_icon(&store, &settings, count - 1, pixels));
        assert(pixels[0] == 0xff336699);
        store_clear(&copy); store_clear(&store);
    }
    free(view.visible);
    puts("Dynamic catalog, deep-copy ownership, last-page navigation and artwork PASS");
}
