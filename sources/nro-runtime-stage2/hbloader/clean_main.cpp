// SPDX-License-Identifier: ISC
// Game-role NRO loader. The entry/config cycle follows nx-hbloader at
// 82b95122c5ae8dc059bf23893ba7623c72c86773; see ../LICENSE.md.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <switch.h>

extern "C" {
#include "switch2/runtime/dynlink.h"
#include "config.h"
#include "cycle.h"
#include "entry.h"
#include "leak_check.h"
#include "builtin_image.h"
extern const unsigned char iz_runtime_start[], iz_runtime_end[];
}
#include <kaihen.h>

namespace {

constexpr char MenuPath[] = "sdmc:/iz2s/izlauncher.iza";
// What the last NRO left behind; see checkAfterReturn.
constexpr char CleanupStatusPath[] = "/iz2s/log/hen/nro-cleanup.txt";
// Where a shared library is looked for after the program's own directory.
constexpr char LibraryDirectory[] = "sdmc:/iz2s/lib";
// The shared libraries of this directory are loaded into every dynamically
// linked program, as Linux's LD_PRELOAD does; see loadPreloads.
constexpr char PreloadDirectory[] = "sdmc:/iz2s/preload";
constexpr size_t LoaderHeapBytes = 64 * 1024 * 1024;
constexpr size_t PageSize = 0x1000;
// The syscalls that the game's policy allows (the ACI0 kernel capabilities of
// the game package's main.npdm): 0x01-0x29, 0x2c, 0x2d, 0x32-0x36 and 0x80.
// Bit n of word n / 64. libnx code chooses its paths from these hints, for
// example jitCreate; a hinted syscall that the policy forbids ends the process.
constexpr u64 GameSyscallHints[3] = {
    UINT64_C(0x007c33fffffffffe),
    0,
    UINT64_C(0x1),
};

alignas(16) u8 g_nro_stack[0x100000];

// The loader's half of the thread's TLS (0x100..0x200), which holds libnx's
// ThreadVars: the reent and thread pointers. An NRO built with libnx's
// runtime replaces them with its own, so restore the loader's after each
// NRO returns, as upstream nx-hbloader does (its source/main.c).
constexpr size_t SavedTlsOffset = 0x100;
constexpr size_t SavedTlsSize = 0x100;
u8 g_saved_tls[SavedTlsSize];
u8* g_heap_base = nullptr;
size_t g_heap_size = 0;
Result g_sm_result = 0;
bool g_sm_open = false;

size_t pageAlign(size_t size) {
    return (size + PageSize - 1) & ~(PageSize - 1);
}

Result getHomebrewSession(Service* out) {
    return kaihenOpenHomebrewSession(ldrRoGetServiceSession(), out);
}

Result openSdFilesystem(Handle root, FsFileSystem* out) {
    Service service = {};
    serviceCreate(&service, root);
    service.own_handle = false;
    return serviceDispatch(&service, 18, .out_num_objects = 1, .out_objects = &out->s);
}

Result readImage(FsFileSystem* sd, const char* path, u8* staging, size_t capacity,
                 NroHeader* header) {
    // The menu remains embedded in IZ2S. An IZA path is never parsed as NRO.
    if (std::strcmp(path, MenuPath) == 0) {
        const size_t size = static_cast<size_t>(iz_runtime_end - iz_runtime_start);
        if (hbloaderCopyBuiltin(staging, capacity, iz_runtime_start, size))
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        std::memcpy(header, staging + sizeof(NroStart), sizeof(*header));
        return 0;
    }
    const char* sd_path = path;
    if (std::strncmp(path, "sdmc:/", 6) == 0) {
        sd_path = path + 5;
    } else if (path[0] != '/') {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    FsFile file = {};
    Result rc = fsFsOpenFile(sd, sd_path, FsOpenMode_Read, &file);
    if (R_FAILED(rc)) {
        return rc;
    }

    s64 file_size = 0;
    rc = fsFileGetSize(&file, &file_size);
    if (R_SUCCEEDED(rc) && file_size < static_cast<s64>(sizeof(NroStart) + sizeof(NroHeader))) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    u64 count = 0;
    if (R_SUCCEEDED(rc)) {
        rc = fsFileRead(&file, 0, staging, sizeof(NroStart) + sizeof(NroHeader), 0, &count);
        if (R_SUCCEEDED(rc) && count != sizeof(NroStart) + sizeof(NroHeader)) {
            rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
    }
    if (R_SUCCEEDED(rc)) {
        std::memcpy(header, staging + sizeof(NroStart), sizeof(*header));
        if (header->magic != NROHEADER_MAGIC || !header->size ||
            header->size > static_cast<u64>(file_size) || header->size > capacity ||
            (header->size | header->bss_size) & (PageSize - 1)) {
            rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
    }
    if (R_SUCCEEDED(rc)) {
        for (u64 offset = 0; offset < header->size; offset += 0x10000) {
            const u64 size = header->size - offset < 0x10000 ? header->size - offset : 0x10000;
            rc = fsFileRead(&file, offset, staging + offset, size, 0, &count);
            if (R_FAILED(rc)) {
                break;
            }
            if (count != size) {
                rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
                break;
            }
        }
    }
    fsFileClose(&file);
    return rc;
}

struct CleanPlatform {
    Service* hen = nullptr;
    FsFileSystem sd = {};
    char loaded_path[HBLOADER_NEXT_PATH_BYTES] = {};
    Handle process = INVALID_HANDLE;
    Handle sm = INVALID_HANDLE;
    Handle nro_root = INVALID_HANDLE;
    // The loaded NRO carries libounce's Switch 2 mark.
    u8* staging = nullptr;
    size_t capacity = 0;
    size_t occupied = 0;

    ~CleanPlatform() {
        if (nro_root != INVALID_HANDLE) {
            svcCloseHandle(nro_root);
        }
        if (sm != INVALID_HANDLE) {
            svcCloseHandle(sm);
        }
        if (sd.s.session != INVALID_HANDLE) {
            fsFsClose(&sd);
        }
        if (process != INVALID_HANDLE) {
            svcCloseHandle(process);
        }
    }
};

//-----------------------------------------------------------------------------
// The images of a program
//-----------------------------------------------------------------------------

// The images of the program that runs now: the program itself, then, for a
// dynamically linked program, the shared libraries it needs, breadth-first.
// libounce's runtime library (libounce.so) links them; see its dynlink.h.
struct ProgramImages {
    // Two arrays of the same length, which grow as a program needs more
    // libraries: the images, and what the loader read from each.
    DynlinkLoadedModule* loaded;
    DynlinkModule* parsed;
    u32 count;
    u32 capacity;
    // What the runtime library gets: the images, and the buffer where it
    // writes why linking failed. The loader also writes there why it could
    // not load a library.
    DynlinkLoadInfo info;
    char message[256];
    // The directories where the program's libraries are looked for, in
    // order: the program's own, then LibraryDirectory. The runtime library
    // gets them for the libraries that the program loads itself.
    char program_directory[HBLOADER_NEXT_PATH_BYTES + 8];
    const char* directories[2];
};
ProgramImages g_images;

// Makes room for one more image in the two arrays. False when the loader has
// no memory for them.
bool reserveImage() {
    if (g_images.count < g_images.capacity) {
        return true;
    }
    const u32 capacity = g_images.capacity != 0 ? g_images.capacity * 2 : 16;
    auto* const loaded = static_cast<DynlinkLoadedModule*>(
        std::realloc(g_images.loaded, capacity * sizeof(DynlinkLoadedModule)));
    if (loaded == nullptr) {
        return false;
    }
    g_images.loaded = loaded;
    auto* const parsed = static_cast<DynlinkModule*>(
        std::realloc(g_images.parsed, capacity * sizeof(DynlinkModule)));
    if (parsed == nullptr) {
        return false;
    }
    g_images.parsed = parsed;
    g_images.capacity = capacity;
    return true;
}

// Reads an NRO file into the staging memory after the images that are
// already there, and maps it. The image's size without its BSS is in
// out_size.
Result loadImage(CleanPlatform* platform, const char* path, u64* out_size) {
    if (!reserveImage()) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    u8* const staging = platform->staging + platform->occupied;
    const size_t capacity = platform->capacity - platform->occupied;
    NroHeader header = {};
    Result rc = readImage(&platform->sd, path, staging, capacity, &header);
    if (R_FAILED(rc)) {
        return rc;
    }
    const size_t occupied = pageAlign(static_cast<size_t>(header.size) + header.bss_size);
    if (occupied >= capacity) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    std::memset(staging + header.size, 0, header.bss_size);

    u64 base = 0;
    rc = ldrRoLoadNro(&base, reinterpret_cast<u64>(staging), header.size,
                      reinterpret_cast<u64>(staging + header.size), header.bss_size);
    if (R_FAILED(rc)) {
        return rc;
    }
    platform->occupied += occupied;

    // A statically linked program has no dynamic symbols to parse: it stays
    // a module that needs no library.
    const u64 size = static_cast<u64>(header.size) + header.bss_size;
    DynlinkModule* const parsed = &g_images.parsed[g_images.count];
    if (R_FAILED(dynlinkModuleOpen(parsed, reinterpret_cast<const void*>(base), size))) {
        *parsed = DynlinkModule{};
    }
    g_images.loaded[g_images.count] = DynlinkLoadedModule{base, size};
    g_images.count++;
    *out_size = header.size;
    return 0;
}

// NROs that a program mapped itself and left: for the last program, and for
// all programs of this process. RO unloaded them with the loader's own.
u32 g_last_left_images = 0;
u32 g_left_images = 0;

// Unloads every NRO of the loader's RO session with one command: the
// program, the libraries that the loader mapped for it, and the ones that
// the program mapped itself and did not unload. The program uses the
// loader's session, so RO has them all in one list. An image that RO cannot
// unmap stays mapped, and the result is that failure.
// @param[out] out_unloaded The number of NROs that RO unloaded.
Result unloadImages(u64* out_unloaded) {
    const u64 pid_placeholder = 0;
    *out_unloaded = 0;
    const Result rc = serviceDispatchInOut(ldrRoGetServiceSession(),
                                           KaihenRoCommand_ReleaseNros,
                                           pid_placeholder, *out_unloaded, .in_send_pid = true);
    if (R_SUCCEEDED(rc)) g_images.count = 0;
    return rc;
}

bool isLibraryLoaded(const char* soname) {
    for (u32 index = 0; index < g_images.count; index++) {
        const char* loaded = g_images.parsed[index].soname;
        if (loaded != nullptr && std::strcmp(loaded, soname) == 0) {
            return true;
        }
    }
    return false;
}

bool isFile(FsFileSystem* sd, const char* path) {
    FsDirEntryType type = FsDirEntryType_Dir;
    return R_SUCCEEDED(fsFsGetEntryType(sd, path, &type)) && type == FsDirEntryType_File;
}

// Sets the directories where the libraries of a program are looked for.
void setLibraryDirectories(const char* program_path) {
    const char* const path = std::strncmp(program_path, "sdmc:", 5) == 0 ? program_path + 5 : program_path;
    const char* const name = std::strrchr(path, '/');
    std::snprintf(g_images.program_directory, sizeof(g_images.program_directory), "sdmc:%.*s",
                  name != nullptr ? static_cast<int>(name - path) : 0, path);
    g_images.directories[0] = g_images.program_directory;
    g_images.directories[1] = LibraryDirectory;
}

// Finds the file of a shared library, which is named by the library's
// DT_SONAME, in the library directories.
bool findLibrary(CleanPlatform* platform, const char* soname, char* path, size_t path_size) {
    if (soname[0] == '\0' || std::strchr(soname, '/') != nullptr) {
        return false;
    }
    for (const char* directory : g_images.directories) {
        const int length = std::snprintf(path, path_size, "%s/%s", directory, soname);
        // The filesystem takes the path without the device name.
        if (length > 0 && static_cast<size_t>(length) < path_size &&
            isFile(&platform->sd, path + std::strlen("sdmc:"))) {
            return true;
        }
    }
    return false;
}

// Loads the shared libraries that the loaded images need, and the ones those
// need. Each image is examined once, in the order it was loaded, so the
// libraries are in breadth-first order. A failure is one of libounce's
// results for a program that did not start (dynlink.h), which the menu shows:
// the reason, with the library's name, is in the message.
Result loadLibraries(CleanPlatform* platform) {
    for (u32 index = 0; index < g_images.count; index++) {
        for (u32 needed = 0; needed < g_images.parsed[index].needed_count; needed++) {
            const char* const soname = dynlinkModuleGetNeeded(&g_images.parsed[index], needed);
            if (isLibraryLoaded(soname)) {
                continue;
            }
            char path[HBLOADER_NEXT_PATH_BYTES];
            if (!findLibrary(platform, soname, path, sizeof(path))) {
                std::snprintf(g_images.message, sizeof(g_images.message),
                              "shared library not found: %s", soname);
                return DYNLINK_RESULT_LIBRARY_NOT_FOUND;
            }
            u64 size = 0;
            const Result rc = loadImage(platform, path, &size);
            if (R_FAILED(rc)) {
                std::snprintf(g_images.message, sizeof(g_images.message),
                              "shared library %s could not be loaded: 0x%x", soname, rc);
                return DYNLINK_RESULT_LIBRARY_NOT_LOADED;
            }
            // The file must be the library it is named for, or the search
            // above would load it again for the next module that needs it.
            if (!isLibraryLoaded(soname)) {
                std::snprintf(g_images.message, sizeof(g_images.message),
                              "the file of shared library %s has another DT_SONAME", soname);
                return DYNLINK_RESULT_LIBRARY_NOT_LOADED;
            }
        }
    }
    return 0;
}

// True for the file name of a shared library: one with ".so" at its end or
// before a version, as libname.so.1. Other files of the directory, such as
// notes, are not loaded.
bool isLibraryName(const char* name) {
    const char* const suffix = std::strstr(name, ".so");
    return suffix != nullptr && (suffix[3] == '\0' || suffix[3] == '.');
}

// Loads the shared libraries of PreloadDirectory in the order of their
// names. They come after the program and before the libraries it needs, so
// that a function they define takes the place of a library's for every
// module that calls it by name; libounce's dlsym(RTLD_NEXT) gives them the
// one they replaced. A library that cannot be loaded stops the program from
// starting, with its name in the cleanup record. To turn one off, move it
// out of the directory. No directory means no such libraries.
Result loadPreloads(CleanPlatform* platform) {
    constexpr size_t NameSize = 256;
    FsDir directory = {};
    if (R_FAILED(fsFsOpenDirectory(&platform->sd, PreloadDirectory + std::strlen("sdmc:"),
                                   FsDirOpenMode_ReadFiles, &directory))) {
        return 0;
    }

    char (*names)[NameSize] = nullptr;
    u32 count = 0;
    u32 capacity = 0;
    Result rc = 0;
    for (;;) {
        static FsDirectoryEntry entry;
        s64 read = 0;
        rc = fsDirRead(&directory, &read, 1, &entry);
        if (R_FAILED(rc) || read == 0) {
            break;
        }
        if (entry.type != FsDirEntryType_File || !isLibraryName(entry.name) ||
            std::strlen(entry.name) >= NameSize) {
            continue;
        }
        if (count == capacity) {
            capacity = capacity != 0 ? capacity * 2 : 8;
            auto* const grown = static_cast<char (*)[NameSize]>(std::realloc(names, capacity * NameSize));
            if (grown == nullptr) {
                rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
                break;
            }
            names = grown;
        }
        std::snprintf(names[count++], NameSize, "%s", entry.name);
    }
    fsDirClose(&directory);

    // The order of the names is the order of the libraries.
    for (u32 index = 1; R_SUCCEEDED(rc) && index < count; index++) {
        char moved[NameSize];
        std::memcpy(moved, names[index], NameSize);
        u32 position = index;
        for (; position != 0 && std::strcmp(names[position - 1], moved) > 0; position--) {
            std::memcpy(names[position], names[position - 1], NameSize);
        }
        std::memcpy(names[position], moved, NameSize);
    }

    for (u32 index = 0; R_SUCCEEDED(rc) && index < count; index++) {
        char path[HBLOADER_NEXT_PATH_BYTES];
        std::snprintf(path, sizeof(path), "%s/%s", PreloadDirectory, names[index]);
        u64 size = 0;
        rc = loadImage(platform, path, &size);
        if (R_SUCCEEDED(rc) && g_images.parsed[g_images.count - 1].soname == nullptr) {
            rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        if (R_FAILED(rc)) {
            std::snprintf(g_images.message, sizeof(g_images.message),
                          "preload library %.180s could not be loaded: 0x%x", names[index], rc);
            rc = DYNLINK_RESULT_PRELOAD_NOT_LOADED;
        }
    }
    std::free(names);
    return rc;
}

// Loads a program and, when it is dynamically linked, its shared libraries.
// The entry of a dynamically linked program is its runtime library's.
Result loadProgram(CleanPlatform* platform, const char* path, u64* out_entry) {
    u64 size = 0;
    Result rc = loadImage(platform, path, &size);
    if (R_FAILED(rc)) {
        return rc;
    }
    const u64 base = g_images.loaded[0].base;

    *out_entry = base;
    if (g_images.parsed[0].needed_count == 0) {
        return 0;
    }

    rc = loadPreloads(platform);
    if (R_SUCCEEDED(rc)) {
        rc = loadLibraries(platform);
    }
    if (R_FAILED(rc)) {
        return rc;
    }
    for (u32 index = 1; index < g_images.count; index++) {
        if (g_images.parsed[index].runtime_entry != 0) {
            *out_entry = g_images.parsed[index].runtime_entry;
            g_images.info = DynlinkLoadInfo{g_images.loaded, g_images.count,
                                            sizeof(g_images.message), g_images.message,
                                            g_images.directories, 2};
            return 0;
        }
    }
    std::snprintf(g_images.message, sizeof(g_images.message),
                  "none of the program's shared libraries is the runtime library (libounce.so)");
    return DYNLINK_RESULT_NO_RUNTIME_LIBRARY;
}

void writeLoadFailure(FsFileSystem* sd, const char* program, Result rc);

Result mapImage(void* opaque, const char* path, HbloaderMappedImage* out) {
    auto* platform = static_cast<CleanPlatform*>(opaque);
    std::snprintf(platform->loaded_path, sizeof(platform->loaded_path), "%s", path);
    // The arrays keep their memory from one program to the next.
    g_images.count = 0;
    g_images.info = DynlinkLoadInfo{};
    g_images.message[0] = '\0';
    setLibraryDirectories(path);
    platform->occupied = 0;

    u64 entry = 0;
    const Result rc = loadProgram(platform, path, &entry);
    if (R_FAILED(rc)) {
        writeLoadFailure(&platform->sd, path, rc);
        u64 unloaded = 0;
        const Result cleanup = unloadImages(&unloaded);
        if (R_FAILED(cleanup)) {
            // A failed unmap leaves live mappings. Tell the cycle that this
            // is terminal; never reuse the staging area for a new menu.
            out->owner = platform;
            std::snprintf(g_images.message, sizeof(g_images.message),
                          "load failed: 0x%x; cleanup failed: 0x%x; restart required", rc, cleanup);
            writeLoadFailure(&platform->sd, path, cleanup);
            return cleanup;
        }
        return rc;
    }
    out->entry = entry;
    out->owner = platform;
    return 0;
}

//-----------------------------------------------------------------------------
// What an NRO left behind
//-----------------------------------------------------------------------------

// The process before the first NRO, and after the last one returned.
HbloaderSnapshot g_baseline;
HbloaderSnapshot g_current;
// Ended threads whose handles the last NRO left open and the loader closed.
u32 g_closed_thread_handles = 0;
// Waiting threads that NROs left behind and the loader parked, in all and for
// the last NRO. Each keeps its thread object until the process ends, so past
// MaxParkedThreads the loader ends the process instead.
u32 g_parked_threads = 0;
u32 g_last_parked_threads = 0;
constexpr u32 MaxParkedThreads = 32;
// Why the loader ended the process over the last NRO's threads, for the
// cleanup record. Empty when it did not.
char g_exit_reason[256] = {};

u32 queryMemory(void*, u64 address, HbloaderMemoryBlock* out) {
    MemoryInfo info = {};
    u32 page = 0;
    const Result rc = svcQueryMemory(&info, &page, address);
    if (R_SUCCEEDED(rc)) {
        *out = HbloaderMemoryBlock{info.addr, info.size, info.type, info.attr, info.perm,
                                   info.ipc_refcount, info.device_refcount};
    }
    return rc;
}

u32 freeThreadCount(void*, u64* out) {
    return svcGetInfo(out, InfoType_FreeThreadCount, CUR_PROCESS_HANDLE, 0);
}

u32 usedMemorySize(void*, u64* out) {
    return svcGetInfo(out, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
}

void takeSnapshot(HbloaderSnapshot* out) {
    const HbloaderProcessQueries queries = {nullptr, queryMemory, freeThreadCount, usedMemorySize};
    const u64 loader_heap = reinterpret_cast<u64>(g_heap_base);
    hbloaderTakeSnapshot(&queries, loader_heap, loader_heap + LoaderHeapBytes, out);
}

// Writes the check of the last NRO to the SD card, replacing the previous one.
bool sameBlock(const HbloaderMemoryBlock& a, const HbloaderMemoryBlock& b) {
    return a.address == b.address && a.size == b.size && a.type == b.type &&
        a.attributes == b.attributes && a.permission == b.permission &&
        a.ipc_references == b.ipc_references && a.device_references == b.device_references;
}

// Appends each block of the current map that the baseline does not have.
int appendChangedBlocks(char* text, size_t capacity, int length) {
    constexpr u32 MaxListed = 32;
    u32 listed = 0;
    for (u32 index = 0; index < g_current.block_count && listed < MaxListed; index++) {
        const HbloaderMemoryBlock& block = g_current.blocks[index];
        bool known = false;
        for (u32 old = 0; old < g_baseline.block_count && !known; old++) {
            known = sameBlock(block, g_baseline.blocks[old]);
        }
        if (known) {
            continue;
        }
        const int added = std::snprintf(text + length, capacity - length,
            "  0x%010llx size 0x%llx type 0x%x attributes 0x%x permission 0x%x ipc %u device %u\n",
            static_cast<unsigned long long>(block.address), static_cast<unsigned long long>(block.size),
            block.type, block.attributes, block.permission, block.ipc_references, block.device_references);
        if (added <= 0 || added >= static_cast<int>(capacity - length)) {
            break;
        }
        length += added;
        listed++;
    }
    return length;
}

void writeCleanupStatus(FsFileSystem* sd, const char* program, const HbloaderLeakReport& leaks) {
    char text[4096];
    int length = std::snprintf(text, sizeof(text),
        "program: %s\n"
        "result: %s\n"
        "threads still in use: %lld%s\n"
        "memory still in use: %lld bytes%s\n"
        "changed memory map blocks: %u%s\n",
        program, hbloaderLeakFound(&leaks) ? "left resources behind" : "clean",
        static_cast<long long>(leaks.threads), leaks.threads_compared ? "" : " (not available)",
        static_cast<long long>(leaks.memory), leaks.memory_compared ? "" : " (not available)",
        leaks.changed_blocks, leaks.map_compared ? "" : " (map not compared)");
    if (length > 0 && length < static_cast<int>(sizeof(text)) && leaks.has_changed_block) {
        const HbloaderMemoryBlock& block = leaks.first_changed_block;
        length += std::snprintf(text + length, sizeof(text) - length,
            "first changed block: address 0x%llx, size 0x%llx, type 0x%x, attributes 0x%x, "
            "permission 0x%x, ipc references %u, device references %u\n",
            static_cast<unsigned long long>(block.address), static_cast<unsigned long long>(block.size),
            block.type, block.attributes, block.permission, block.ipc_references, block.device_references);
        if (length > 0 && length < static_cast<int>(sizeof(text))) {
            length += std::snprintf(text + length, sizeof(text) - length, "changed blocks:\n");
            length = appendChangedBlocks(text, sizeof(text), length);
        }
    }
    if (length > 0 && length < static_cast<int>(sizeof(text)) && g_exit_reason[0] != '\0') {
        length += std::snprintf(text + length, sizeof(text) - length, "%s\n", g_exit_reason);
    }
    // The runtime library returned without calling the program's main.
    if (length > 0 && length < static_cast<int>(sizeof(text)) && g_images.message[0] != '\0') {
        length += std::snprintf(text + length, sizeof(text) - length,
                                "the program did not start: %s\n", g_images.message);
    }
    if (length > 0 && length < static_cast<int>(sizeof(text)) && g_last_parked_threads != 0) {
        length += std::snprintf(text + length, sizeof(text) - length,
            "threads the program left running (paused by the loader): %u, %u in this process; "
            "free threads %llu, %llu before the first program\n",
            g_last_parked_threads, g_parked_threads,
            static_cast<unsigned long long>(g_current.free_threads),
            static_cast<unsigned long long>(g_baseline.free_threads));
    }
    if (length > 0 && length < static_cast<int>(sizeof(text)) && g_closed_thread_handles != 0) {
        length += std::snprintf(text + length, sizeof(text) - length,
            "ended threads the program did not close (released by the loader): %u\n",
            g_closed_thread_handles);
    }
    if (length > 0 && length < static_cast<int>(sizeof(text)) && g_left_images != 0) {
        length += std::snprintf(text + length, sizeof(text) - length,
            "shared libraries the program left loaded (unloaded by the loader): %u, %u in this process\n",
            g_last_left_images, g_left_images);
    }
    if (length <= 0 || length >= static_cast<int>(sizeof(text))) {
        return;
    }

    fsFsDeleteFile(sd, CleanupStatusPath);
    if (R_FAILED(fsFsCreateFile(sd, CleanupStatusPath, length, 0))) {
        return;
    }
    FsFile file = {};
    if (R_SUCCEEDED(fsFsOpenFile(sd, CleanupStatusPath, FsOpenMode_Write, &file))) {
        fsFileWrite(&file, 0, text, length, FsWriteOption_Flush);
        fsFileClose(&file);
    }
}

// Replaces the cleanup record with the reason that a program was not loaded.
void writeLoadFailure(FsFileSystem* sd, const char* program, Result rc) {
    char text[1024];
    const int length = std::snprintf(text, sizeof(text),
        "program: %s\nresult: not loaded (0x%x)\n%s%s",
        program, rc, g_images.message, g_images.message[0] != '\0' ? "\n" : "");
    if (length <= 0 || length >= static_cast<int>(sizeof(text))) {
        return;
    }
    fsFsDeleteFile(sd, CleanupStatusPath);
    if (R_FAILED(fsFsCreateFile(sd, CleanupStatusPath, length, 0))) {
        return;
    }
    FsFile file = {};
    if (R_SUCCEEDED(fsFsOpenFile(sd, CleanupStatusPath, FsOpenMode_Write, &file))) {
        fsFileWrite(&file, 0, text, length, FsWriteOption_Flush);
        fsFileClose(&file);
    }
}

// Compares the process after an NRO returned and was unloaded with the
// baseline, and reports what is left: threads, memory, and mappings such as
// stack mirrors and the heap pages they lock.
void checkAfterReturn(CleanPlatform* platform) {
    takeSnapshot(&g_current);
    HbloaderLeakReport leaks;
    hbloaderCompareSnapshots(&g_baseline, &g_current, &leaks);
    writeCleanupStatus(&platform->sd, platform->loaded_path, leaks);
}

// Unmaps the JIT code that the returned NRO left executable. Its source pages
// are in the heap that the next NRO uses.
Result releaseJitCode() {
    const u64 pid_placeholder = 0;
    u64 released = 0;
    Result rc = serviceDispatchInOut(ldrRoGetServiceSession(),
                                     KaihenRoCommand_ReleaseJitCode,
                                     pid_placeholder, released, .in_send_pid = true);
    return rc;
}

//-----------------------------------------------------------------------------
// Threads that a returned NRO left behind
//-----------------------------------------------------------------------------

// libnx and libounce-nx keep these 0x20 bytes at the end of each thread's
// 0x200-byte TLS slot (source/internal.h). threadExit leaves them in place,
// and the kernel clears a slot only when a new thread takes it.
struct ThreadVars {
    u32 magic;
    Handle handle;
    u64 thread;
    u64 reent;
    u64 tls_tp;
};
static_assert(sizeof(ThreadVars) == 0x20);
constexpr u32 ThreadVarsMagic = 0x21545624; // "!TV$"
constexpr u64 ThreadLocalSlotSize = 0x200;

// The start of a libnx-style Thread (include/switch/kernel/thread.h), which
// ThreadVars points to: enough to undo threadCreate's stack mirror.
struct ThreadStart {
    Handle handle;
    bool owns_stack_mem;
    u64 stack_mem;
    u64 stack_mirror;
};
static_assert(__builtin_offsetof(ThreadStart, stack_mem) == 8);
static_assert(__builtin_offsetof(ThreadStart, stack_mirror) == 16);

struct NroThreads {
    u32 running = 0;    ///< Still running and not parked: the image must stay mapped.
    u32 parked = 0;     ///< Was still running; the loader paused it for good.
    u32 closed = 0;     ///< Ended with its handle left open; the loader released it.
    u32 unreleased = 0; ///< Ended, but its stack mirror could not be undone.
};

// True when every byte of the range is mapped readable. The range may span
// several blocks of the memory map: neighbouring pages in different states
// are separate blocks.
bool isReadable(u64 address, u64 size) {
    const u64 end = address + size;
    while (address < end) {
        MemoryInfo info = {};
        u32 page_info = 0;
        if (R_FAILED(svcQueryMemory(&info, &page_info, address)) || info.type == MemType_Unmapped ||
            !(info.perm & Perm_R) || info.addr + info.size <= address) {
            return false;
        }
        address = info.addr + info.size;
    }
    return true;
}

// Unmaps a thread's stack mirror, as threadClose does. The mirror otherwise
// keeps the stack's heap pages locked. The Thread is checked against the
// handle first, and the mirror's size comes from the memory map;
// svcUnmapMemory itself refuses a source that is not the mirror's.
bool unmapStackMirror(const ThreadVars& vars) {
    ThreadStart thread = {};
    if (vars.thread == 0 || (vars.thread & 7) != 0 || !isReadable(vars.thread, sizeof(thread))) {
        return false;
    }
    std::memcpy(&thread, reinterpret_cast<const void*>(vars.thread), sizeof(thread));
    if (thread.handle != vars.handle) {
        return false;
    }
    if (thread.stack_mirror == 0) {
        return true;
    }
    MemoryInfo mirror = {};
    u32 page_info = 0;
    if (R_FAILED(svcQueryMemory(&mirror, &page_info, thread.stack_mirror)) ||
        mirror.type != MemType_MappedMemory || mirror.addr != thread.stack_mirror) {
        return false;
    }
    return R_SUCCEEDED(svcUnmapMemory(reinterpret_cast<void*>(thread.stack_mirror),
                                      reinterpret_cast<void*>(thread.stack_mem), mirror.size));
}

// Does what threadClose would have done for an ended thread: unmaps its stack
// mirror and closes its handle. False when the mirror could not be undone.
bool releaseEndedThread(const ThreadVars& vars) {
    if (!unmapStackMirror(vars)) {
        return false;
    }
    svcCloseHandle(vars.handle);
    return true;
}

// Pauses a thread that the NRO left running. Reading its context confirms
// that the pause took effect. The thread stays paused: either it is parked,
// or the loader ends the process.
bool pauseLeftThread(Handle handle) {
    ThreadContext context = {};
    return R_SUCCEEDED(svcSetThreadActivity(handle, ThreadActivity_Paused)) &&
        R_SUCCEEDED(svcGetThreadContext3(&context, handle));
}

// Parks a thread that the NRO left running, instead of ending the process
// over it. Switch 1's loader loads the next NRO over such a thread; SDL's
// timer thread is a common one: SDL_AddTimer starts it, and SDL_Quit leaves
// it waiting on its semaphore with no timeout. Here the thread stays paused,
// so it can never run the unmapped image, even if its wait ends. Whatever it
// held is in the returned program's image and heap, which nothing uses
// again. Its stack mirror is unmapped, so that the stack's heap pages return
// to the next program. The thread object remains until the process ends.
// False when it could not be paused or its stack mirror undone.
bool parkLeftThread(u64 slot, const ThreadVars& vars) {
    if (!pauseLeftThread(vars.handle) || !unmapStackMirror(vars)) {
        return false;
    }
    // Later scans must not take this slot for a thread: the handle is closed
    // here, and its value can name another object later.
    auto* slot_vars = reinterpret_cast<ThreadVars*>(slot + ThreadLocalSlotSize - sizeof(ThreadVars));
    slot_vars->magic = 0;
    svcCloseHandle(vars.handle);
    return true;
}

// True when the process has more threads than before the first NRO, apart
// from the threads the loader parked.
bool threadsLeftBehind() {
    takeSnapshot(&g_current);
    return g_baseline.has_free_threads && g_current.has_free_threads &&
        g_current.free_threads + g_parked_threads < g_baseline.free_threads;
}

// Finds the threads that libnx-style runtimes created, through the ThreadVars
// in every TLS slot except the loader's own (the NRO ran on the loader's
// thread). Releases those that have ended, parks those that still run, and
// counts those it could not park. A slot keeps the values of a thread whose
// object is already gone, so only a handle that still names a thread is
// acted on.
NroThreads handleNroThreads() {
    NroThreads threads;
    const u64 own_slot = reinterpret_cast<u64>(armGetTls()) & ~(ThreadLocalSlotSize - 1);
    const Handle main_thread = envGetMainThreadHandle();
    u64 address = 0;
    for (;;) {
        MemoryInfo info = {};
        u32 page_info = 0;
        if (R_FAILED(svcQueryMemory(&info, &page_info, address))) {
            break;
        }
        if (info.type == MemType_ThreadLocal && (info.perm & Perm_R)) {
            for (u64 slot = info.addr; slot + ThreadLocalSlotSize <= info.addr + info.size;
                 slot += ThreadLocalSlotSize) {
                if (slot == own_slot) {
                    continue;
                }
                ThreadVars vars;
                std::memcpy(&vars, reinterpret_cast<const void*>(slot + ThreadLocalSlotSize - sizeof(vars)),
                            sizeof(vars));
                if (vars.magic != ThreadVarsMagic || vars.handle == INVALID_HANDLE ||
                    vars.handle == main_thread) {
                    continue;
                }
                // Only a handle that still names a thread is acted on.
                u64 thread_id = 0;
                if (R_FAILED(svcGetThreadId(&thread_id, vars.handle))) {
                    continue;
                }
                // A thread handle is signaled once the thread has ended.
                s32 index = -1;
                if (R_FAILED(svcWaitSynchronization(&index, &vars.handle, 1, 0))) {
                    // Still running: park it, unless the process already
                    // holds as many parked threads as the loader allows. If
                    // not, pause it; the loader ends the process.
                    if (g_parked_threads + threads.parked < MaxParkedThreads &&
                        parkLeftThread(slot, vars)) {
                        threads.parked++;
                    } else {
                        pauseLeftThread(vars.handle);
                        threads.running++;
                    }
                    continue;
                }
                if (releaseEndedThread(vars)) {
                    threads.closed++;
                } else {
                    threads.unreleased++;
                }
            }
        }
        const u64 next = info.addr + info.size;
        if (next <= address) {
            break;
        }
        address = next;
    }
    return threads;
}

Result unloadImage(void* opaque, const HbloaderMappedImage*) {
    auto* platform = static_cast<CleanPlatform*>(opaque);

    // A returned NRO may still own threads. One that has ended but that the
    // NRO never closed is harmless once released: its stack mirror unmapped
    // and its handle closed, as threadClose would. One that still runs may be
    // executing this image, and unmapping it would leave that thread running
    // freed code. The loader parks such a thread: it pauses it for good (see
    // parkLeftThread). When it cannot, or the process holds too many parked
    // threads, or the check cannot account for every thread (ones created
    // without a libnx-style runtime), the process is the ownership boundary:
    // end it while the image is still mapped, and let the kernel retire the
    // threads.
    g_closed_thread_handles = 0;
    g_last_parked_threads = 0;
    g_exit_reason[0] = '\0';
    if (threadsLeftBehind()) {
        const NroThreads threads = handleNroThreads();
        g_closed_thread_handles = threads.closed;
        g_last_parked_threads = threads.parked;
        g_parked_threads += threads.parked;
        if (threads.running != 0 || threads.unreleased != 0 || threadsLeftBehind()) {
            // Why the process ends, for the cleanup record.
            if (threads.running != 0 || threads.unreleased != 0) {
                std::snprintf(g_exit_reason, sizeof(g_exit_reason),
                    "the loader ended the process: the program left %u threads running "
                    "that it could not park and %u ended threads it could not release",
                    threads.running, threads.unreleased);
            } else {
                std::snprintf(g_exit_reason, sizeof(g_exit_reason),
                    "the loader ended the process: the program left threads that were not "
                    "created by a libnx-style runtime, which the loader cannot find");
            }
            HbloaderLeakReport leaks;
            hbloaderCompareSnapshots(&g_baseline, &g_current, &leaks);
            writeCleanupStatus(&platform->sd, platform->loaded_path, leaks);
            svcExitProcess();
        }
    }

    Result rc = releaseJitCode();
    if (R_FAILED(rc)) {
        return rc;
    }

    // What RO unloads beyond the loader's own images is what the program
    // mapped and left.
    const u64 loaded_by_loader = g_images.count;
    u64 unloaded = 0;
    rc = unloadImages(&unloaded);
    g_last_left_images = unloaded > loaded_by_loader ? static_cast<u32>(unloaded - loaded_by_loader) : 0;
    g_left_images += g_last_left_images;
    if (R_SUCCEEDED(rc)) {
        checkAfterReturn(platform);
    }
    return rc;
}

Result prepareConfig(void* opaque, HbloaderConfigInput* out) {
    auto* platform = static_cast<CleanPlatform*>(opaque);
    static char notice[] = "ounce-hbloader";
    out->main_thread_handle = envGetMainThreadHandle();
    out->process_handle = platform->process;
    out->fs_root_handle = platform->nro_root;
    out->ro_handle = ldrRoGetServiceSession()->session;
    // Each program gets a session of its own on HEN's SM proxy: one that
    // closes it, or makes SM close it, leaves the next program its own.
    if (platform->sm != INVALID_HANDLE) {
        svcCloseHandle(platform->sm);
        platform->sm = INVALID_HANDLE;
    }
    Result sm_rc = henGetServiceManager(platform->hen, &platform->sm);
    if (R_FAILED(sm_rc)) {
        return sm_rc;
    }
    out->sm_handle = platform->sm;
    out->fs_ldr_handle = INVALID_HANDLE;
    // No debug BSD session: the NRO is the process's only BSD client.
    out->debug_bsd_handle = INVALID_HANDLE;
    out->debug_socket = -1;
    // The runtime library of a dynamically linked program gets the images.
    out->modules_info = g_images.info.module_count != 0 ? reinterpret_cast<u64>(&g_images.info) : 0;
    out->heap_address = reinterpret_cast<u64>(platform->staging + platform->occupied);
    out->heap_size = platform->capacity - platform->occupied;
    out->syscall_hints[0] = GameSyscallHints[0];
    out->syscall_hints[1] = GameSyscallHints[1];
    out->syscall_hints[2] = GameSyscallHints[2];
    out->hos_version = MAKEHOSVERSION(20, 0, 0);
    out->is_application = 1;
    out->notice = notice;
    out->notice_size = sizeof(notice);
    return 0;
}

Result enterImage(void*, u64 entry, const HbloaderConfigEntry* entries,
                  u64 return_handle, u32* out_result) {
    // Each NRO opens its own SM session. The loader has acquired everything
    // needed for the full cycle, so release its session before the first entry.
    if (g_sm_open) {
        smExit();
        g_sm_open = false;
    }
    u8* const tls = static_cast<u8*>(armGetTls());
    std::memcpy(g_saved_tls, tls + SavedTlsOffset, SavedTlsSize);
    *out_result = hbloaderSwitch2Enter(entries, return_handle, entry,
                                       g_nro_stack + sizeof(g_nro_stack));
    std::memcpy(tls + SavedTlsOffset, g_saved_tls, SavedTlsSize);
    return 0;
}

Result runMenu(Service* hen) {
    CleanPlatform clean;
    clean.hen = hen;
    Result rc = henGetProcessHandle(hen, &clean.process);
    if (R_FAILED(rc)) {
        return rc;
    }

    Handle root = INVALID_HANDLE;
    rc = henGetFilesystemRoot(hen, &root);
    if (R_FAILED(rc)) {
        return rc;
    }
    rc = openSdFilesystem(root, &clean.sd);
    svcCloseHandle(root);
    if (R_FAILED(rc)) {
        return rc;
    }

    fsFsCreateDirectory(&clean.sd, "/iz2s/log");
    fsFsCreateDirectory(&clean.sd, "/iz2s/log/hen");
    rc = henGetFilesystemRoot(hen, &clean.nro_root);
    if (R_FAILED(rc)) {
        return rc;
    }
    clean.staging = g_heap_base + LoaderHeapBytes;
    clean.capacity = g_heap_size - LoaderHeapBytes;
    // ReleaseNros belongs only to this serial, outer NRO cycle. It is never
    // called when one IZA Split pane exits inside the live embedded menu.
    u64 unloaded = 0;
    rc = unloadImages(&unloaded);
    if (R_FAILED(rc)) {
        std::snprintf(g_images.message, sizeof(g_images.message),
                      "the running RO does not support this loader (0x%x); install the matching save and restart the console", rc);
        writeLoadFailure(&clean.sd, MenuPath, rc);
        return rc;
    }
    // The process as each NRO should leave it.
    takeSnapshot(&g_baseline);
    HbloaderCycle cycle = {};
    HbloaderCyclePlatform platform = {
        &clean, mapImage, unloadImage, prepareConfig, enterImage,
    };

    for (;;) {
        const bool running_menu = cycle.next_path[0] == '\0';
        const HbloaderCycleResult ran = hbloaderSwitch2Step(&cycle, &platform, MenuPath);
        if (ran.stage != HBLOADER_CYCLE_OK) {
            rc = ran.result ? ran.result : MAKERESULT(Module_Libnx, LibnxError_BadInput);
            break;
        }
        if (running_menu && cycle.next_path[0] == '\0') {
            break;
        }
    }

    if (cycle.has_mapping) {
        Result unload = unloadImage(&clean, &cycle.mapped);
        if (R_SUCCEEDED(rc)) {
            rc = unload;
        }
    }
    return rc;
}

} // namespace

extern "C" void __libnx_initheap(void) {
    u64 total = 0;
    u64 used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    size_t size = 0;
    if (total > used + 0x200000) {
        size = (total - used - 0x200000) & ~static_cast<size_t>(0x1fffff);
    }
    if (size == 0) {
        size = 0x20000000;
    }
    void* base = nullptr;
    if (R_FAILED(svcSetHeapSize(&base, size)) || size <= LoaderHeapBytes) {
        return;
    }
    g_heap_base = static_cast<u8*>(base);
    g_heap_size = size;
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = static_cast<char*>(base);
    fake_heap_end = fake_heap_start + LoaderHeapBytes;
}

extern "C" void __appInit(void) {
    g_sm_result = smInitialize();
    g_sm_open = R_SUCCEEDED(g_sm_result);
}

extern "C" void __appExit(void) {
    if (g_sm_open) {
        smExit();
    }
}

int main() {
    if (R_FAILED(g_sm_result) || !g_heap_base) {
        return 1;
    }

    Result rc = ldrRoInitialize();
    if (R_FAILED(rc)) {
        return 1;
    }

    Service hen = {};
    rc = getHomebrewSession(&hen);
    if (R_SUCCEEDED(rc)) {
        HenStatus status = {};
        rc = henGetStatus(&hen, &status);
        if (R_SUCCEEDED(rc)) {
            rc = runMenu(&hen);
        }
        serviceClose(&hen);
    }
    ldrRoExit();
    return R_FAILED(rc) ? 1 : 0;
}
