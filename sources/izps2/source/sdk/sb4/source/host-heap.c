#include <sb4/host-heap.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#ifdef __SWITCH2__
#include <sys/reent.h>
#endif

typedef struct {
    void *(*allocate)(size_t);
    void *(*allocate_zero)(size_t, size_t);
    void *(*resize)(void *, size_t);
    void (*release)(void *);
    void *(*aligned_alloc)(size_t, size_t);
    void *(*memalign)(size_t, size_t);
    int (*posix_memalign)(void **, size_t, size_t);
    int *(*errno_location)(void);
    uint64_t (*free_bytes)(void);
} HostedHeap;

static HostedHeap heap;
static atomic_uint bound;

uint32_t sb4HostHeapBind(const Sb4Compat *compat) {
    if (!sb4CompatValid(compat)) return EINVAL;
    HostedHeap candidate = {
        .allocate = (void *(*)(size_t))compat->resolve_c("malloc"),
        .allocate_zero = (void *(*)(size_t, size_t))compat->resolve_c("calloc"),
        .resize = (void *(*)(void *, size_t))compat->resolve_c("realloc"),
        .release = (void (*)(void *))compat->resolve_c("free"),
        .aligned_alloc = (void *(*)(size_t, size_t))compat->resolve_c("aligned_alloc"),
        .memalign = (void *(*)(size_t, size_t))compat->resolve_c("memalign"),
        .posix_memalign = (int (*)(void **, size_t, size_t))compat->resolve_c("posix_memalign"),
        .errno_location = (int *(*)(void))compat->resolve_c("__errno_location"),
        .free_bytes = (uint64_t (*)(void))compat->resolve_c("heapGetFreeSize"),
    };
    if (!candidate.allocate || !candidate.allocate_zero || !candidate.resize || !candidate.release ||
        !candidate.aligned_alloc || !candidate.memalign || !candidate.posix_memalign ||
        !candidate.errno_location)
        return ENOSYS;

    unsigned expected = 0;
    if (!atomic_compare_exchange_strong(&bound, &expected, 1)) return EBUSY;
    heap = candidate;
    atomic_store_explicit(&bound, 2, memory_order_release);
    return 0;
}

static int ready(void) {
    return atomic_load_explicit(&bound, memory_order_acquire) == 2;
}

uint64_t __wrap_heapGetFreeSize(void) {
    return ready() && heap.free_bytes ? heap.free_bytes() : 0;
}

static void *check_allocation(void *result, int requested) {
    if (!result && requested) errno = *heap.errno_location();
    return result;
}

void *__wrap_malloc(size_t bytes) {
    return ready() ? check_allocation(heap.allocate(bytes), bytes != 0) : NULL;
}

void *__wrap_calloc(size_t items, size_t bytes) {
    return ready() ? check_allocation(heap.allocate_zero(items, bytes), items && bytes) : NULL;
}

void *__wrap_realloc(void *allocation, size_t bytes) {
    return ready() ? check_allocation(heap.resize(allocation, bytes), bytes != 0) : NULL;
}

void __wrap_free(void *allocation) {
    if (allocation && ready()) heap.release(allocation);
}

void *__wrap_aligned_alloc(size_t alignment, size_t bytes) {
    return ready() ? check_allocation(heap.aligned_alloc(alignment, bytes), bytes != 0) : NULL;
}

void *__wrap_memalign(size_t alignment, size_t bytes) {
    return ready() ? check_allocation(heap.memalign(alignment, bytes), bytes != 0) : NULL;
}

int __wrap_posix_memalign(void **allocation, size_t alignment, size_t bytes) {
    return ready() ? heap.posix_memalign(allocation, alignment, bytes) : ENOMEM;
}

#ifdef __SWITCH2__
static void *reentrant_result(struct _reent *context, void *result, int requested) {
    if (!result && requested) context->_errno = ready() ? *heap.errno_location() : ENOMEM;
    return result;
}

void *__wrap__malloc_r(struct _reent *context, size_t bytes) {
    void *result = ready() ? heap.allocate(bytes) : NULL;
    return reentrant_result(context, result, bytes != 0);
}

void *__wrap__calloc_r(struct _reent *context, size_t items, size_t bytes) {
    void *result = ready() ? heap.allocate_zero(items, bytes) : NULL;
    return reentrant_result(context, result, items && bytes);
}

void *__wrap__realloc_r(struct _reent *context, void *allocation, size_t bytes) {
    void *result = ready() ? heap.resize(allocation, bytes) : NULL;
    return reentrant_result(context, result, bytes != 0);
}

void __wrap__free_r(struct _reent *context, void *allocation) {
    (void)context;
    __wrap_free(allocation);
}

void *__wrap__memalign_r(struct _reent *context, size_t alignment, size_t bytes) {
    void *result = ready() ? heap.memalign(alignment, bytes) : NULL;
    return reentrant_result(context, result, bytes != 0);
}
#endif
