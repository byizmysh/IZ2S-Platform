// Bounded probes using a real process handle transferred by IPC.
typedef unsigned int u32;
typedef unsigned long long u64;
typedef struct { u64 result, value; } Reply;
typedef struct {
    u64 address, size;
    u32 type, attributes, permission, ipc_refs, device_refs, padding;
} MemoryInfo;

extern Reply process_id(u32 handle);
extern Reply get_info(u32 type, u32 handle, u64 subtype);
extern Reply query_memory(MemoryInfo *info, u64 address);
extern Reply query_process(MemoryInfo *info, u32 handle, u64 address);
extern u32 map_process(u64 destination, u32 handle, u64 source, u64 size);
extern u32 unmap_process(u64 destination, u32 handle, u64 source, u64 size);
extern u32 map_code(u32 handle, u64 destination, u64 source, u64 size);
extern u32 unmap_code(u32 handle, u64 destination, u64 source, u64 size);
extern u32 process_permission(u32 handle, u64 address, u64 size, u32 permission);

#define SELF 0xffff8001u
#define BAD_INPUT 0xd401u

static void publish_code(u64 address, u64 size) {
    // Cover every possible cache line without assuming a particular line size.
    for (u64 i = 0; i < size; i += 4)
        __asm__ volatile("dc cvau, %0" :: "r"(address + i) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    for (u64 i = 0; i < size; i += 4)
        __asm__ volatile("ic ivau, %0" :: "r"(address + i) : "memory");
    __asm__ volatile("dsb ish; isb" ::: "memory");
}

static void copy_info(u64 *out, const MemoryInfo *info) {
    out[0] = info->address; out[1] = info->size;
    out[2] = info->type | ((u64)info->attributes << 32);
    out[3] = info->permission | ((u64)info->ipc_refs << 32);
    out[4] = info->device_refs;
}

static u64 free_address(u32 handle, u64 size) {
    Reply begin = get_info(12, handle, 0), extent = get_info(13, handle, 0);
    Reply alias = get_info(2, handle, 0), alias_size = get_info(3, handle, 0);
    Reply heap = get_info(4, handle, 0), heap_size = get_info(5, handle, 0);
    if (begin.result || extent.result || alias.result || alias_size.result || heap.result || heap_size.result)
        return 0;
    u64 at = begin.value, end = at + extent.value;
    if (!at) at = 0x1000;
    for (u32 i = 0; i < 256 && at < end; i++) {
        MemoryInfo info = {0};
        Reply query = handle == SELF ? query_memory(&info, at) : query_process(&info, handle, at);
        if (query.result || !info.size || info.address + info.size <= at)
            return 0;
        u64 next = info.address + info.size;
        if (at < alias.value + alias_size.value && at + size > alias.value) {
            at = alias.value + alias_size.value; continue;
        }
        if (at < heap.value + heap_size.value && at + size > heap.value) {
            at = heap.value + heap_size.value; continue;
        }
        if (info.type == 0 && at + size <= next && at + size <= end) return at;
        at = next;
    }
    return 0;
}

u32 process_request(u32 command, const u64 *input, u32 bytes,
    u32 handle, u64 *out, u32 *output_bytes, void *receive, u64 receive_size) {
    *output_bytes = 64;
    if (bytes < 32) return BAD_INPUT;
    if (receive_size && command != 0x524f0016) return BAD_INPUT;
    Reply pid = process_id(handle), program = get_info(18, handle, 0);
    out[0] = pid.value; out[1] = program.value;
    out[2] = pid.result | (program.result << 32);
    if (pid.result) return pid.result;
    if (program.result) return program.result; // Also rejects non-process handles.
    if (input[0] && input[0] != pid.value) return BAD_INPUT;
    MemoryInfo info = {0};
    Reply queried = query_process(&info, handle, input[1]);
    out[3] = queried.result;
    if (command == 0x524f0010) {
        copy_info(out + 4, &info); *output_bytes = 104;
        out[9] = get_info(12, handle, 0).value;
        out[10] = get_info(13, handle, 0).value;
        out[11] = free_address(handle, 0x1000);
        out[12] = free_address(SELF, 0x1000);
        return queried.result;
    }
    if (!input[0] || queried.result) return queried.result ? queried.result : BAD_INPUT;
    if (command == 0x524f0015) {
        // Toggle only the read bit of native FS executable pages.
        u64 address = input[1], size = input[2], permission = input[3];
        if (program.value != 0x0400000000000000ULL || (address | size) & 0xfff ||
            !size || size > 0x4000000 || info.type != 3 || info.attributes ||
            (info.permission != 4 && info.permission != 5) ||
            (permission != 4 && permission != 5) ||
            address + size < address || address + size > info.address + info.size)
            return BAD_INPUT;
        out[3] = info.permission;
        out[4] = process_permission(handle, address, size, (u32)permission);
        if (out[4]) return out[4];
        MemoryInfo after = {0};
        Reply q = query_process(&after, handle, address);
        out[5] = q.result; copy_info(out + 6, &after); *output_bytes = 88;
        return q.result;
    }
    if (command == 0x524f0016) {
        // Copy at most 4 MiB through a temporary readable alias in RO.
        u64 source = input[1], length = input[2], page = source & ~0xfffULL;
        if (!receive || !length || length > 0x400000 || length > receive_size ||
            source + length < source || source + length > info.address + info.size ||
            (info.type != 3 && info.type != 4) || info.attributes)
            return BAD_INPUT;
        u64 size = ((source & 0xfff) + length + 0xfff) & ~0xfffULL;
        u64 local = free_address(SELF, size);
        out[3] = local;
        if (!local) return BAD_INPUT;
        out[4] = map_process(local, handle, page, size);
        if (out[4]) return out[4];
        volatile unsigned char *mapped = (volatile unsigned char *)(local + (source & 0xfff));
        unsigned char *destination = (unsigned char *)receive;
        for (u64 i = 0; i < length; i++) destination[i] = mapped[i];
        out[5] = unmap_process(local, handle, page, size);
        out[6] = length;
        return out[5];
    }
    if (command == 0x524f0014) {
        // One aligned instruction block, with exact expected bytes and readback.
        u64 address = input[1], size = input[2], page = address & ~0xfffULL;
        *output_bytes = 80;
        if (bytes < 64 || size != 16 || address & 3 || (address & 0xfff) + size > 0x1000 ||
            !(info.permission & 4) || info.attributes || address + size > info.address + info.size)
            return BAD_INPUT;
        u64 local = free_address(SELF, 0x1000);
        if (!local) return BAD_INPUT;
        out[9] = local;
        out[4] = map_process(local, handle, page, 0x1000);
        if (out[4]) return out[4];
        MemoryInfo alias = {0};
        Reply q = query_memory(&alias, local);
        out[5] = alias.permission | ((u64)alias.attributes << 32);
        u32 result = q.result;
        if (!result && (alias.permission != 3 || alias.attributes)) result = BAD_INPUT;
        volatile unsigned char *mapped = (volatile unsigned char *)(local + (address & 0xfff));
        const unsigned char *expected = (const unsigned char *)(input + 4);
        const unsigned char *replacement = (const unsigned char *)(input + 6);
        if (!result) {
            out[6] = 1;
            for (u32 i = 0; i < size; i++) if (mapped[i] != expected[i]) out[6] = 0;
            if (!out[6]) result = BAD_INPUT;
        }
        if (!result) {
            volatile u32 *instructions = (volatile u32 *)mapped;
            const u32 *new_instructions = (const u32 *)replacement;
            for (u32 i = 0; i < size / 4; i++) instructions[i] = new_instructions[i];
            publish_code((u64)mapped, size);
            out[7] = 1;
            for (u32 i = 0; i < size; i++) if (mapped[i] != replacement[i]) out[7] = 0;
            if (!out[7]) result = BAD_INPUT;
        }
        out[8] = unmap_process(local, handle, page, 0x1000);
        return out[8] ? out[8] : result;
    }
    if (command == 0x524f0011) {
        u64 source = input[1], length = input[2], page = source & ~0xfffULL;
        if (!length || length > 128 || (source & 0xfff) + length > 0x1000) return BAD_INPUT;
        u64 local = free_address(SELF, 0x1000);
        out[3] = local;
        if (!local) return BAD_INPUT;
        out[4] = map_process(local, handle, page, 0x1000);
        if (out[4]) return out[4];
        volatile unsigned char *mapped = (volatile unsigned char *)(local + (source & 0xfff));
        unsigned char *data = (unsigned char *)(out + 7);
        for (u32 i = 0; i < length; i++) data[i] = mapped[i];
        out[5] = unmap_process(local, handle, page, 0x1000);
        out[6] = length; *output_bytes = 56 + length;
        return out[5];
    }
    if (command == 0x524f0012) {
        // One caller-owned Normal/RW page; preserve the source if protection fails.
        u64 source = input[1], destination = input[2], size = input[3];
        if ((source | destination) & 0xfff || size != 0x1000 || info.type != 5 ||
            info.permission != 3 || info.attributes || source + size > info.address + info.size)
            return BAD_INPUT;
        MemoryInfo target = {0};
        Reply q = query_process(&target, handle, destination);
        if (q.result || target.type || destination + size > target.address + target.size)
            return q.result ? q.result : BAD_INPUT;
        out[3] = map_code(handle, destination, source, size);
        if (out[3]) return out[3];
        out[4] = process_permission(handle, destination, size, 5);
        if (out[4]) out[5] = unmap_code(handle, destination, source, size);
        return out[4];
    }
    if (command == 0x524f0013) {
        u64 source = input[1], destination = input[2], size = input[3];
        if ((source | destination) & 0xfff || size != 0x1000) return BAD_INPUT;
        return unmap_code(handle, destination, source, size);
    }
    return BAD_INPUT;
}
