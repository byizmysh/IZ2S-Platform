/*
 * memory.c - mmap/munmap for the Linux personality.
 *
 * Pages come from chunks: Vulkan host-visible *cached* memory (the large pool
 * described by the upstream runtime as a multi-GiB pool) and, as a fallback,
 * the SDK heap. Actual capacity must be measured in the current process.  Uncached memory is never used: exclusive and
 * atomic accesses on it can fault.  Each chunk keeps a sorted list of free
 * page runs; freeing coalesces.  Linux guarantees zeroed anonymous memory, so
 * pages are cleared on allocation.
 */
#include "hbxl.h"
#include "hbx_sdk.h"
#include "hbx_vk.h"
#include "hbxl_stack_pool.h"

_Static_assert(__builtin_offsetof(struct hbx_vk_dispatch, vkGetInstanceProcAddr) ==
               HBXL_DISPATCH_GIPA * sizeof(void *), "dispatch layout");

#define PAGE 4096u
#define MAX_CHUNKS 96
#define MAX_RUNS 1024
#define VK_CHUNK (64ull << 20)
#define SDK_CHUNK (16ull << 20)
#define SDK_STACK_BLOCK ((8ull << 20) + 65536)
#define SDK_STACK_BLOCKS 2

struct run { uint32_t start, pages; };

struct chunk {
    uint8_t *base;
    uint64_t pages;
    uint64_t used;
    int kind;                    /* 1 cached Vulkan, 2 reserved SDK, 4 ordinary SDK fallback */
    VkDeviceMemory memory;
    uint32_t nruns;
    struct run runs[MAX_RUNS];
};

static struct chunk g_chunks[MAX_CHUNKS];
static int g_nchunks;
static volatile int g_lock;
static int32_t g_vk_type = -1;
static int g_vk_failed;
static uint64_t g_used_pages;
static uint64_t g_vk_reserved, g_vk_peak, g_vk_heap_bytes;
static int g_memory_poisoned;
static uint32_t g_stack_creators[128];
static uint32_t g_stack_creator_count;
uint64_t g_sdk_stack_maps;
uint64_t g_sdk_stack_scopes_entered;
uint64_t g_sdk_failed_requests, g_sdk_last_request;
int g_sdk_last_result;

static void lock(void) {
    while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) hbxl_svc_sleep(0);
}
static void unlock(void) { __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }

uint64_t hbxl_mem_used(void) { return g_used_pages * PAGE; }

void hbxl_memory_snapshot(Ps2HbxlMemorySnapshot *out) {
    if (!out) return;
    lock();
    *out = (Ps2HbxlMemorySnapshot){0};
    out->sdk_failed_requests = g_sdk_failed_requests;
    out->sdk_last_request = g_sdk_last_request;
    out->sdk_last_result = g_sdk_last_result;
    out->stack_scopes = g_stack_creator_count;
    out->poisoned = g_memory_poisoned;
    for (int i = 0; i < g_nchunks; i++) {
        const struct chunk *c = &g_chunks[i];
        if (c->kind == 1) {
            out->vk_reserved += c->pages * PAGE;
            out->vk_used += c->used * PAGE;
        } else if (c->kind == 2 || c->kind == 4) {
            out->sdk_chunks++;
            out->sdk_reserved += c->pages * PAGE;
            out->sdk_used += c->used * PAGE;
            for (uint32_t j = 0; j < c->nruns; j++) {
                const uint64_t bytes = (uint64_t)c->runs[j].pages * PAGE;
                if (bytes > out->sdk_largest_free) out->sdk_largest_free = bytes;
            }
        }
    }
    unlock();
}

static int add_chunk_vk_exact(uint64_t bytes) {
    const struct hbx_vk_dispatch *d = (const struct hbx_vk_dispatch *)(uintptr_t)hbxl_ctx->gpu.dispatch;
    VkDevice device = (VkDevice)(uintptr_t)hbxl_ctx->gpu.device;
    VkMemoryAllocateInfo info;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *p = 0;
    struct chunk *c;
    if (g_memory_poisoned || g_vk_type < 0 || !d || !device || g_nchunks >= MAX_CHUNKS ||
        bytes > g_vk_heap_bytes || g_vk_reserved > g_vk_heap_bytes - bytes) return -1;
    hbxl_memset(&info, 0, sizeof info);
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = bytes;
    info.memoryTypeIndex = (uint32_t)g_vk_type;
    VkResult result = d->vkAllocateMemory(device, &info, NULL, &memory);
    if (result != VK_SUCCESS) {
        g_vk_failed = 1;
        if (memory != VK_NULL_HANDLE) goto uncertain;
        return -1;
    }
    if (memory == VK_NULL_HANDLE) goto uncertain;
    result = d->vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &p);
    if (result != VK_SUCCESS) {
        d->vkFreeMemory(device, memory, NULL);
        g_vk_failed = 1;
        return -1;
    }
    if (!p || ((uintptr_t)p & (PAGE - 1)) || bytes > UINTPTR_MAX - (uintptr_t)p)
        goto uncertain;
    hbxl_ring_logf("hbxl: cached vulkan chunk %u MiB at 0x%x", bytes >> 20, (uint64_t)(uintptr_t)p);
    c = &g_chunks[g_nchunks++];
    hbxl_memset(c, 0, sizeof *c);
    c->base = (uint8_t *)p;
    c->pages = bytes / PAGE;
    c->kind = 1;
    c->memory = memory;
    c->nruns = 1;
    c->runs[0].start = 0;
    c->runs[0].pages = (uint32_t)c->pages;
    g_vk_reserved += bytes;
    if (g_vk_reserved > g_vk_peak) g_vk_peak = g_vk_reserved;
    g_vk_failed = 0;
    return g_nchunks - 1;
uncertain:
    /* Preserve the exact object and all backing memory. A normal error with
     * a null output is retryable; a malformed success/dirty output is not. */
    c = &g_chunks[g_nchunks++];
    hbxl_memset(c, 0, sizeof *c);
    c->kind = 3; c->memory = memory; c->base = p; c->pages = bytes / PAGE;
    g_memory_poisoned = 1;
    return -1;
}

static int add_chunk_vk(uint64_t minimum) {
    uint64_t bytes = minimum > VK_CHUNK ? minimum : VK_CHUNK;
    if (g_vk_type < 0 || minimum > g_vk_heap_bytes || g_memory_poisoned) return -1;
    if (bytes > g_vk_heap_bytes - g_vk_reserved) bytes = minimum;
    for (;;) {
        int index = add_chunk_vk_exact(bytes);
        if (index >= 0 || g_memory_poisoned || bytes == minimum) return index;
        uint64_t next = bytes / 2;
        if (next < minimum) next = minimum;
        bytes = next;
    }
}

static int add_chunk_sdk(uint64_t bytes, int kind) {
    void *p = 0;
    struct chunk *c;
    if (g_nchunks >= MAX_CHUNKS) return -1;
    g_sdk_last_request = bytes;
    g_sdk_last_result = HBXL_CALL(IMP_MEMALIGN, int (*)(void **, uint64_t, uint64_t))(&p, PAGE, bytes);
    if (g_sdk_last_result || !p) {
        g_sdk_failed_requests++;
        // A non-null result on failure is an ownership ambiguity, not a reason
        // to allocate again. Keep every object for diagnosis in that case.
        if (p || !g_sdk_last_result) g_memory_poisoned = 1;
        return -1;
    }
    hbxl_ring_logf("hbxl: sdk heap chunk %u MiB at 0x%x", bytes >> 20, (uint64_t)(uintptr_t)p);
    c = &g_chunks[g_nchunks++];
    hbxl_memset(c, 0, sizeof *c - sizeof c->runs);
    c->base = (uint8_t *)p;
    c->pages = bytes / PAGE;
    c->kind = kind;
    c->nruns = 1;
    c->runs[0].start = 0;
    c->runs[0].pages = (uint32_t)c->pages;
    return g_nchunks - 1;
}

static int add_sdk_at_least(uint64_t minimum, int kind) {
    uint64_t bytes = minimum > SDK_CHUNK ? minimum : SDK_CHUNK;
    for (;;) {
        const int index = add_chunk_sdk(bytes, kind);
        if (index >= 0 || g_memory_poisoned || bytes == minimum) return index;
        uint64_t next = (bytes / 2) & ~(uint64_t)(PAGE - 1);
        bytes = next < minimum ? minimum : next;
    }
}

int hbxl_mem_init(void) {
    const struct hbx_vk_dispatch *d = (const struct hbx_vk_dispatch *)(uintptr_t)hbxl_ctx->gpu.dispatch;
    if (g_nchunks || g_memory_poisoned) return -1;
    g_vk_type = -1; g_vk_heap_bytes = 0; g_vk_failed = 0;
    if (d && hbxl_ctx->gpu.physical && hbxl_ctx->gpu.device &&
        d->vkGetPhysicalDeviceMemoryProperties && d->vkAllocateMemory &&
        d->vkMapMemory && d->vkUnmapMemory && d->vkFreeMemory) {
        VkPhysicalDeviceMemoryProperties props;
        /* These allocations are CPU-only; HOST_COHERENT controls CPU/GPU
         * cache maintenance, not coherence among CPU cores. HOST_CACHED is
         * required for allocator atomics. Prefer coherent when sizes tie. */
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        unsigned selected_coherent = 0;
        hbxl_memset(&props, 0, sizeof props);
        d->vkGetPhysicalDeviceMemoryProperties((VkPhysicalDevice)(uintptr_t)hbxl_ctx->gpu.physical, &props);
        if (props.memoryTypeCount <= 32 && props.memoryHeapCount <= 16) {
            for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
                uint32_t heap = props.memoryTypes[i].heapIndex;
                if ((props.memoryTypes[i].propertyFlags & want) != want || heap >= props.memoryHeapCount) continue;
                uint64_t available = props.memoryHeaps[heap].size;
                /* Driver heap size is a policy ceiling, not live free memory.
                 * Keep each chunk's uint32_t page-run fields representable. */
                const uint64_t page_bound = (uint64_t)UINT32_MAX * PAGE;
                if (available > page_bound) available = page_bound;
                available &= ~(uint64_t)(PAGE - 1);
                unsigned coherent = !!(props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                if (available >= PAGE && (available > g_vk_heap_bytes ||
                    (available == g_vk_heap_bytes && coherent > selected_coherent))) {
                    g_vk_type = (int32_t)i; g_vk_heap_bytes = available; selected_coherent = coherent;
                }
            }
        }
        hbxl_logf("hbxl: CPU cached Vulkan type %d, runtime pool cap %u MiB", (int64_t)g_vk_type, g_vk_heap_bytes >> 20);
    }
    // Reserve SDK stack/IPC headroom BEFORE libc and the PS2 core can run.
    // Separate blocks avoid demanding a single 32 MiB hole. Each block can
    // hold an 8 MiB stack plus its guard/TLS metadata. A failure here follows
    // the existing return-code-3 path, before guest Main/GS/audio ownership.
    for (unsigned i = 0; i < SDK_STACK_BLOCKS; i++) {
        if (add_chunk_sdk(SDK_STACK_BLOCK, 2) < 0) {
            hbxl_mem_release_all();
            return -1;
        }
    }
    return 0;
}

static void *take(struct chunk *c, uint32_t pages) {
    uint32_t i;
    for (i = 0; i < c->nruns; i++) {
        if (c->runs[i].pages >= pages) {
            uint8_t *p = c->base + (uint64_t)c->runs[i].start * PAGE;
            c->runs[i].start += pages;
            c->runs[i].pages -= pages;
            if (!c->runs[i].pages) {
                hbxl_memcpy(&c->runs[i], &c->runs[i + 1], (c->nruns - i - 1) * sizeof(struct run));
                c->nruns--;
            }
            c->used += pages;
            return p;
        }
    }
    return 0;
}

int hbxl_stack_scope_enter(void) {
    const uint32_t tid = (uint32_t)hbxl_gettid();
    int free_slot = -1;
    if (!tid) return -HBXL_EINVAL;
    lock();
    for (int i = 0; i < 128; i++) {
        if (g_stack_creators[i] == tid) { unlock(); return -HBXL_EAGAIN; }
        if (!g_stack_creators[i] && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0 || g_memory_poisoned) { unlock(); return -HBXL_EAGAIN; }
    g_stack_creators[free_slot] = tid;
    g_sdk_stack_scopes_entered++;
    __atomic_add_fetch(&g_stack_creator_count, 1, __ATOMIC_RELEASE);
    unlock();
    return 0;
}

int hbxl_stack_scope_leave(void) {
    const uint32_t tid = (uint32_t)hbxl_gettid();
    lock();
    for (int i = 0; i < 128; i++) if (g_stack_creators[i] == tid) {
        g_stack_creators[i] = 0;
        __atomic_sub_fetch(&g_stack_creator_count, 1, __ATOMIC_RELEASE);
        unlock(); return 0;
    }
    unlock(); return -HBXL_EINVAL;
}

uint32_t hbxl_stack_scopes_live(void) { return __atomic_load_n(&g_stack_creator_count, __ATOMIC_ACQUIRE); }

static void *map_pages(size_t length, int sdk_only) {
    if (!length || length > SIZE_MAX - (PAGE - 1)) return 0;
    size_t count = (length + PAGE - 1) / PAGE;
    if (count > UINT32_MAX) return 0;
    uint32_t pages = (uint32_t)count;
    void *p = 0;
    int i;
    lock();
    if (g_memory_poisoned) { unlock(); return 0; }
    for (i = 0; i < g_nchunks && !p; i++) {
        const int kind = g_chunks[i].kind;
        const int eligible = sdk_only ? (kind == 2 || kind == 4) :
            (kind == 1 || kind == 4 || (g_vk_type < 0 && kind == 2));
        if (eligible) p = take(&g_chunks[i], pages);
    }
    if (!p) {
        uint64_t bytes = (uint64_t)pages * PAGE;
        int k = -1;
        if (!sdk_only) k = add_chunk_vk(bytes);
        if (k < 0 && !g_memory_poisoned) k = add_sdk_at_least(bytes, sdk_only ? 2 : 4);
        if (k >= 0) p = take(&g_chunks[k], pages);
    }
    if (p) g_used_pages += pages;
    if (p && sdk_only) g_sdk_stack_maps++;
    unlock();
    if (p) __builtin_memset(p, 0, (size_t)pages * PAGE);
    return p;
}

void *hbxl_mmap_sdk(size_t length) { return map_pages(length, 1); }

void *hbxl_mmap(size_t length) {
    int sdk_only = 0;
    if (__atomic_load_n(&g_stack_creator_count, __ATOMIC_ACQUIRE)) {
        const uint32_t tid = (uint32_t)hbxl_gettid();
        lock();
        for (int i = 0; i < 128; i++) if (g_stack_creators[i] == tid) { sdk_only = 1; break; }
        unlock();
    }
    return map_pages(length, sdk_only);
}

int hbxl_munmap(void *address, size_t length) {
    uint8_t *a = (uint8_t *)address;
    if (!length || length > SIZE_MAX - (PAGE - 1) ||
        (length + PAGE - 1) / PAGE > UINT32_MAX || ((uintptr_t)a & (PAGE - 1))) return -HBXL_EINVAL;
    uint32_t pages = (uint32_t)((length + PAGE - 1) / PAGE);
    int i;
    lock();
    if (g_memory_poisoned) { unlock(); return -HBXL_ENOMEM; }
    for (i = 0; i < g_nchunks; i++) {
        struct chunk *c = &g_chunks[i];
        uint32_t start, k;
        if (a < c->base || a >= c->base + c->pages * PAGE) continue;
        start = (uint32_t)((uint64_t)(a - c->base) / PAGE);
        if (pages > c->pages - start) { unlock(); return -HBXL_EINVAL; }
        for (k = 0; k < c->nruns && c->runs[k].start < start; k++) {}
        /* Ignore double frees / overlaps with free runs. */
        if (k > 0 && c->runs[k - 1].start + c->runs[k - 1].pages > start) break;
        if (k < c->nruns && start + pages > c->runs[k].start) break;
        if (k > 0 && c->runs[k - 1].start + c->runs[k - 1].pages == start) {
            c->runs[k - 1].pages += pages;
            if (k < c->nruns && c->runs[k - 1].start + c->runs[k - 1].pages == c->runs[k].start) {
                c->runs[k - 1].pages += c->runs[k].pages;
                hbxl_memcpy(&c->runs[k], &c->runs[k + 1], (c->nruns - k - 1) * sizeof(struct run));
                c->nruns--;
            }
        } else if (k < c->nruns && start + pages == c->runs[k].start) {
            c->runs[k].start = start;
            c->runs[k].pages += pages;
        } else if (c->nruns < MAX_RUNS) {
            uint32_t m;
            for (m = c->nruns; m > k; m--) c->runs[m] = c->runs[m - 1];
            c->runs[k].start = start;
            c->runs[k].pages = pages;
            c->nruns++;
        } else { unlock(); return -HBXL_ENOMEM; }
        c->used -= pages;
        g_used_pages -= pages;
        break;
    }
    unlock();
    return 0;
}

void hbxl_mem_release_all(void) {
    const struct hbx_vk_dispatch *d = (const struct hbx_vk_dispatch *)(uintptr_t)hbxl_ctx->gpu.dispatch;
    VkDevice device = (VkDevice)(uintptr_t)hbxl_ctx->gpu.device;
    int i;
    if (g_memory_poisoned) {
        hbxl_logf("hbxl: memory ownership unknown; retaining %u chunks", (uint64_t)g_nchunks);
        return;
    }
    for (i = 0; i < g_nchunks; i++) {
        if (g_chunks[i].kind == 1 && d && device) {
            d->vkUnmapMemory(device, g_chunks[i].memory);
            d->vkFreeMemory(device, g_chunks[i].memory, NULL);
        } else if (g_chunks[i].kind == 2 || g_chunks[i].kind == 4) {
            HBXL_CALL(IMP_FREE, void (*)(void *))(g_chunks[i].base);
        }
    }
    hbxl_memset(g_chunks, 0, sizeof g_chunks);
    g_nchunks = 0; g_used_pages = 0; g_vk_reserved = 0;
}
