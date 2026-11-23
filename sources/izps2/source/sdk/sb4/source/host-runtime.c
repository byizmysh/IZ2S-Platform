#include <sb4/host-heap.h>
#include <sb4/host-runtime.h>
#include <errno.h>
#include <stdatomic.h>

/* Also supplied by crtbegin when the consumer's link includes it. */
__attribute__((weak, visibility("hidden"))) void *__dso_handle = &__dso_handle;

void *__wrap_malloc(size_t size);
void __wrap_free(void *allocation);

typedef struct ExitHandler {
    struct ExitHandler *next;
    void *handle, *argument;
    void (*destructor)(void *);
    void (*plain)(void);
} ExitHandler;

enum Phase { COLD, INITIALIZING, LIVE, FINALIZING, FINISHED };
static enum Phase phase;
static Sb4HostHooks image_hooks;
static ExitHandler *handlers;
static int registration_failed;
static atomic_flag registry_lock = ATOMIC_FLAG_INIT;

static void lock_registry(void) {
    while (atomic_flag_test_and_set_explicit(&registry_lock, memory_order_acquire)) {}
}

static void unlock_registry(void) {
    atomic_flag_clear_explicit(&registry_lock, memory_order_release);
}

static int register_handler(ExitHandler entry) {
    ExitHandler *record = __wrap_malloc(sizeof(*record));
    lock_registry();
    const int active = phase == INITIALIZING || phase == LIVE || phase == FINALIZING;
    if (!active || !record) {
        if (active) registration_failed = 1;
        unlock_registry();
        __wrap_free(record);
        return -1;
    }
    entry.next = handlers;
    *record = entry;
    handlers = record;
    unlock_registry();
    return 0;
}

int __wrap_atexit(void (*function)(void)) {
    return function ? register_handler((ExitHandler){.handle = __dso_handle, .plain = function}) : -1;
}

int __wrap___cxa_atexit(void (*function)(void *), void *argument, void *handle) {
    return function ? register_handler((ExitHandler){
        .handle = handle, .argument = argument, .destructor = function,
    }) : -1;
}

void __wrap___cxa_finalize(void *handle) {
    for (;;) {
        lock_registry();
        ExitHandler **cursor = &handlers;
        while (*cursor && handle && (*cursor)->handle != handle)
            cursor = &(*cursor)->next;
        ExitHandler *record = *cursor;
        if (record) *cursor = record->next;
        unlock_registry();
        if (!record) return;

        const ExitHandler entry = *record;
        __wrap_free(record);
        if (entry.destructor) entry.destructor(entry.argument);
        else entry.plain();
    }
}

static void forward(const Sb4HostHook *array, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (array[i]) array[i]();
}

uint32_t sb4HostRuntimeInitialize(const Sb4Compat *compat, const Sb4HostHooks *hooks) {
    if (!hooks || (hooks->preinit_count && !hooks->preinit) ||
        (hooks->init_count && !hooks->init) || (hooks->fini_count && !hooks->fini))
        return EINVAL;
    lock_registry();
    if (phase != COLD) {
        unlock_registry();
        return EBUSY;
    }
    phase = INITIALIZING;
    unlock_registry();

    const uint32_t status = sb4HostHeapBind(compat);
    if (status) {
        lock_registry();
        phase = COLD;
        unlock_registry();
        return status;
    }
    image_hooks = *hooks;
    forward(image_hooks.preinit, image_hooks.preinit_count);
    forward(image_hooks.init, image_hooks.init_count);

    lock_registry();
    phase = LIVE;
    const uint32_t result = registration_failed ? SB4_HOST_RUNTIME_RETAINED : 0;
    unlock_registry();
    return result;
}

uint32_t sb4HostRuntimeFinalize(void) {
    lock_registry();
    if (phase != LIVE) {
        unlock_registry();
        return EBUSY;
    }
    phase = FINALIZING;
    unlock_registry();

    __wrap___cxa_finalize(NULL);
    for (size_t i = image_hooks.fini_count; i; --i)
        if (image_hooks.fini[i - 1]) image_hooks.fini[i - 1]();
    __wrap___cxa_finalize(NULL);

    lock_registry();
    phase = FINISHED;
    const uint32_t result = registration_failed ? SB4_HOST_RUNTIME_RETAINED : 0;
    unlock_registry();
    return result;
}
