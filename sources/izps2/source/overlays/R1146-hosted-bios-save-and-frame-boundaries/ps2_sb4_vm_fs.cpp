#include "ps2_sb4_layout.h"
#include "ps2_sb4_vm_fs.h"
#include "ps2_sb4_fs.h"
#include "ps2_sb4_sd_saves.h"
#include "hbxl_vm_frontend.h"
#include "fs_bridge.h"
#include "common/HostVFS.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

uint32_t ps2_sb4_vm_fs_ensure_vm_directory(const char *path);
extern "C" void izps2_boot_stage(IzNativeApp *, const char *, uint32_t);
#define BOOT(text) izps2_boot_stage(nullptr, text "\n", sizeof(text))

namespace {
constexpr char BiosAlias[] = PS2_SB4_SDK_ROOT "/bios";
constexpr char GamesRoot[] = PS2_SB4_SDK_ROOT "/games";
constexpr char ResourcesAlias[] = "/resources";
constexpr char CardsPath[] = "/" PS2_SB4_RUNTIME_DIR "/memcards";
constexpr char StatesPath[] = "/" PS2_SB4_RUNTIME_DIR "/sstates";
constexpr char RuntimePath[] = "/" PS2_SB4_RUNTIME_DIR;
constexpr char FirmwarePath[] = PS2_SB4_RUNTIME_DIR "/firmware";
constexpr char CacheSuffix[] = "/cache";
constexpr char RuntimeSdPath[] = "sdmc:" PS2_SB4_SD_ROOT "/" PS2_SB4_RUNTIME_DIR;
constexpr char GzipPath[] = "/" PS2_SB4_RUNTIME_DIR "/gzip-index";
constexpr char PreviousSuffix[] = ".iz2s-previous";
constexpr char KeepBackupSuffix[] = ".iz2s-backup";
constexpr char BackupSuffix[] = ".backup";
constexpr size_t GamesRootSize = sizeof(GamesRoot) - 1;
Ps2SdkFs media;
char bios_root[PS2_FS_PATH_MAX];
size_t bios_root_size;
char resources_root[PS2_FS_PATH_MAX];
size_t resources_root_size;
char card_root[PS2_FS_PATH_MAX];
size_t card_root_size;
char state_os_root[PS2_FS_PATH_MAX], state_vfs_root[PS2_FS_PATH_MAX];
char runtime_vfs_root[PS2_FS_PATH_MAX], gzip_vfs_root[PS2_FS_PATH_MAX];
size_t state_os_size;
std::thread::id state_owner;
decltype(HostVFS::Ops::open) bridge_open;
bool has_disc;
bool disc_reads_on_worker;
bool installed;
std::atomic_uint open_directories;

bool at_or_under(const char *path, const char *root, size_t size) {
    return !std::strncmp(path, root, size) && (!path[size] || path[size] == '/');
}

bool in_resources(const char *path) {
    return resources_root_size && at_or_under(path, resources_root, resources_root_size);
}
bool is_read_only(const char *path) {
    return at_or_under(path, bios_root, bios_root_size) || at_or_under(path, GamesRoot, GamesRootSize) ||
        in_resources(path);
}
bool media_enabled(const char *path) { return has_disc || !at_or_under(path, "games", sizeof("games") - 1); }

bool in_cards(const char *path) {
    return card_root_size && at_or_under(path, card_root, card_root_size);
}

bool scoped_state_path(const char *path) {
    return path && state_os_size && at_or_under(path, state_os_root, state_os_size) &&
        std::this_thread::get_id() == state_owner;
}

bool valid_suffix(std::string_view tail) {
    while (!tail.empty()) {
        tail.remove_prefix(1);
        const auto slash = tail.find('/');
        const auto part = tail.substr(0, slash);
        if (part.empty() || part == "." || part == ".." || part.find_first_of("\\:") != part.npos)
            return false;
        if (slash == tail.npos) break;
        tail.remove_prefix(slash);
    }
    return true;
}

const char *map_path(const char *tail, const char *root, char (&mapped)[PS2_FS_PATH_MAX]) {
    if (!valid_suffix(tail)) return nullptr;
    const int length = std::snprintf(mapped, sizeof(mapped), "%s%s", root, tail);
    return length > 0 && static_cast<size_t>(length) < sizeof(mapped) ? mapped : nullptr;
}

const char *runtime_sd_suffix(const char *path) {
    constexpr size_t prefix = sizeof(RuntimeSdPath) - 1;
    return runtime_vfs_root[0] && at_or_under(path, RuntimeSdPath, prefix) ? path + prefix : nullptr;
}

const char *media_path(const char *path, char (&mapped)[PS2_FS_PATH_MAX]) {
    if (at_or_under(path, bios_root, bios_root_size)) return path + bios_root_size - (sizeof("bios") - 1);
    if (at_or_under(path, GamesRoot, GamesRootSize)) return path + GamesRootSize - (sizeof("games") - 1);
    return in_resources(path) ? map_path(path + resources_root_size, ResourcesAlias, mapped) : nullptr;
}

const char *card_path(const char *path, char (&mapped)[PS2_FS_PATH_MAX]) {
    return map_path(path + card_root_size, CardsPath, mapped);
}

uint32_t exists(const std::string &path, bool &found) {
    uint64_t ignored;
    const uint32_t result = ps2_sb4_fs_path_size(path.c_str(), &ignored);
    found = !result;
    return ps2_sb4_fs_is_not_found(result) ? 0 : result;
}

uint32_t remove_if_present(const std::string &path) {
    bool found;
    const uint32_t result = exists(path, found);
    return !result && found ? ps2_sb4_fs_remove_save(path.c_str()) : result;
}

uint32_t recover_previous(const std::string &target) {
    const std::string old = target + PreviousSuffix;
    const std::string keep = target + KeepBackupSuffix;
    bool discard_old, keep_old, primary;
    uint32_t result = exists(old, discard_old);
    if (result) return result;
    result = exists(keep, keep_old);
    if (result || (!discard_old && !keep_old)) return result;
    if (discard_old && keep_old) return PS2_FS_INVALID;
    const auto &previous = keep_old ? keep : old;
    result = exists(target, primary);
    if (result) return result;
    if (!primary) {
        result = ps2_sb4_fs_rename_save(previous.c_str(), target.c_str());
    } else {
        // A prior commit may have failed after the new file became visible.
        result = ps2_sb4_fs_commit_saves();
        if (result) return result;
        if (keep_old) {
            const std::string backup = target + BackupSuffix;
            result = remove_if_present(backup);
            if (!result) result = ps2_sb4_fs_rename_save(previous.c_str(), backup.c_str());
        } else {
            result = ps2_sb4_fs_remove_save(previous.c_str());
        }
    }
    return result ? result : ps2_sb4_fs_commit_saves();
}

uint32_t recover_for_read(const char *mapped) {
    std::string_view path(mapped);
    if (path.ends_with(BackupSuffix)) path.remove_suffix(sizeof(BackupSuffix) - 1);
    return path.ends_with(".p2s") ? recover_previous(std::string(path)) : 0;
}

bool state_stage_name(std::string_view path) {
    if (!path.ends_with(Ps2Sb4StateStageSuffix)) return false;
    path.remove_suffix(sizeof(Ps2Sb4StateStageSuffix) - 1);
    return path.ends_with(".p2s");
}

void *open_module_file(const char *path, unsigned access, unsigned hints) {
    if (!path) return nullptr;
    if (!scoped_state_path(path)) {
        if (const char *suffix = runtime_sd_suffix(path)) {
            char mapped[PS2_FS_PATH_MAX];
            return map_path(suffix, runtime_vfs_root, mapped) ? bridge_open(mapped, access, hints) : nullptr;
        }
        return bridge_open(path, access, hints);
    }
    const std::string_view filename(path);
    if (access != PS2_FS_READ && !state_stage_name(filename)) return nullptr;
    char mapped[PS2_FS_PATH_MAX];
    if (access == PS2_FS_READ && map_path(path + state_os_size, StatesPath, mapped)) recover_for_read(mapped);
    return map_path(path + state_os_size, state_vfs_root, mapped) ? bridge_open(mapped, access, hints) : nullptr;
}

bool read_on_opening_thread(const char *path) {
    if (!path) return false;
    if (in_cards(path) || scoped_state_path(path) || runtime_sd_suffix(path) ||
        at_or_under(path, bios_root, bios_root_size) || in_resources(path))
        return true;
    return has_disc && !disc_reads_on_worker && at_or_under(path, GamesRoot, GamesRootSize);
}

int stat_path(const char *path, s32 *size) {
    if (!path || !size) return 0;
    char mapped[PS2_FS_PATH_MAX];
    const bool card = in_cards(path);
    const bool state = scoped_state_path(path);
    const char *runtime = runtime_sd_suffix(path);
    const char *name = card ? card_path(path, mapped) :
        state ? map_path(path + state_os_size, StatesPath, mapped) :
        runtime ? map_path(runtime, RuntimePath, mapped) : media_path(path, mapped);
    if (!name || (!card && !state && !media_enabled(name))) return 0;
    if (state) recover_for_read(name);
    Sb4StorageInfo info{};
    if (ps2_sb4_fs_stat(name, &info)) return 0;
    *size = info.size > INT32_MAX ? INT32_MAX : static_cast<s32>(info.size);
    return HostVFS::STAT_IS_VALID | (info.directory ? HostVFS::STAT_IS_DIRECTORY : 0);
}

int mkdir_path(const char *path) {
    if (!path) return -1;
    if (runtime_sd_suffix(path) || in_cards(path))
        return ps2_sb4_vm_fs_ensure_vm_directory(path) ? -1 : 0;
    if (is_read_only(path)) {
        s32 size;
        return stat_path(path, &size) & HostVFS::STAT_IS_DIRECTORY ? -2 : -1;
    }
    if (!mkdir(path, 0777)) return 0;
    struct stat info {};
    return errno == EEXIST && !stat(path, &info) && S_ISDIR(info.st_mode) ? -2 : -1;
}

int remove_path(const char *path) {
    if (!path) return -1;
    char mapped[PS2_FS_PATH_MAX];
    if (scoped_state_path(path)) {
        return map_path(path + state_os_size, StatesPath, mapped) && !recover_for_read(mapped) &&
            !ps2_sb4_fs_remove_save(mapped) && !ps2_sb4_fs_commit_saves() ? 0 : -1;
    }
    if (in_cards(path)) return card_path(path, mapped) && !ps2_sb4_fs_remove_save(mapped) ? 0 : -1;
    if (const char *runtime = runtime_sd_suffix(path))
        return map_path(runtime, RuntimePath, mapped) && !ps2_sb4_fs_remove_save(mapped) &&
            !ps2_sb4_fs_commit_saves() ? 0 : -1;
    return is_read_only(path) ? -1 : unlink(path);
}

int rename_path(const char *from, const char *to) {
    if (!from || !to) return -1;
    const char *runtime_from = runtime_sd_suffix(from);
    const char *runtime_to = runtime_sd_suffix(to);
    if (runtime_from || runtime_to) {
        char source_path[PS2_FS_PATH_MAX], destination_path[PS2_FS_PATH_MAX];
        if (!runtime_from || !runtime_to || !*runtime_from || !*runtime_to ||
            !map_path(runtime_from, RuntimePath, source_path) ||
            !map_path(runtime_to, RuntimePath, destination_path)) return -1;
        constexpr size_t cache_length = sizeof(CacheSuffix) - 1;
        if (at_or_under(runtime_from, CacheSuffix, cache_length) &&
            at_or_under(runtime_to, CacheSuffix, cache_length)) {
            const uint32_t result = ps2_sb4_fs_replace_cache(from, to);
            if (result != PS2_FS_UNSUPPORTED) return result ? -1 : 0;
        }
        return !ps2_sb4_fs_rename_save(source_path, destination_path) &&
            !ps2_sb4_fs_commit_saves() ? 0 : -1;
    }
    const bool source = in_cards(from), destination = in_cards(to);
    if (source != destination) return -1;
    if (!source) return is_read_only(from) || is_read_only(to) ? -1 : std::rename(from, to);
    char source_path[PS2_FS_PATH_MAX], destination_path[PS2_FS_PATH_MAX];
    return card_path(from, source_path) && card_path(to, destination_path) &&
        !ps2_sb4_fs_rename_save(source_path, destination_path) ? 0 : -1;
}

struct Directory {
    std::unique_ptr<Sb4StorageEntry[]> entries;
    uint32_t count = 0, next = 0, token = 0;
    DIR *local = nullptr;
    char local_path[PATH_MAX];
    const char *name = nullptr;
    bool is_directory = false, include_hidden = false, finished = false;
};

void *open_directory(const char *path, bool include_hidden) {
    if (!path || !*path) return nullptr;
    std::unique_ptr<Directory> dir(new (std::nothrow) Directory);
    if (!dir) return nullptr;
    char mapped[PS2_FS_PATH_MAX];
    const bool card = in_cards(path);
    const char *runtime = runtime_sd_suffix(path);
    const char *name = card ? card_path(path, mapped) :
        runtime ? map_path(runtime, RuntimePath, mapped) : media_path(path, mapped);
    if (card || runtime || is_read_only(path)) {
        if (!name || (!card && !media_enabled(name))) return nullptr;
        const uint32_t result = ps2_sb4_fs_open_dir(name, &dir->token);
        if (result && result != PS2_FS_UNSUPPORTED) return nullptr;
        if (!result && !dir->token) return nullptr;
        const uint32_t capacity = dir->token ? SB4_STORAGE_PAGE_MAX : SB4_STORAGE_LIST_MAX;
        dir->entries.reset(new (std::nothrow) Sb4StorageEntry[capacity]);
        if (!dir->entries) {
            if (dir->token) ps2_sb4_fs_close_dir(dir->token);
            return nullptr;
        }
        if (!dir->token && (ps2_sb4_fs_list(name, dir->entries.get(), capacity, &dir->count) ||
                            dir->count > capacity)) return nullptr;
    } else {
        const size_t length = std::strlen(path);
        if (length >= sizeof(dir->local_path)) return nullptr;
        std::memcpy(dir->local_path, path, length + 1);
        dir->local = opendir(path);
        if (!dir->local) return nullptr;
    }
    dir->include_hidden = include_hidden;
    ++open_directories;
    return dir.release();
}

bool visible(const char *name, bool include_hidden) {
    return name[0] && std::strcmp(name, ".") && std::strcmp(name, "..") &&
        (include_hidden || name[0] != '.');
}

bool read_directory(void *handle) {
    auto &dir = *static_cast<Directory *>(handle);
    dir.name = nullptr;
    if (dir.local) {
        while (const auto *item = readdir(dir.local)) {
            if (!visible(item->d_name, dir.include_hidden)) continue;
            char path[PATH_MAX];
            const int length = std::snprintf(path, sizeof(path), "%s/%s", dir.local_path, item->d_name);
            struct stat info;
            if (length < 0 || static_cast<size_t>(length) >= sizeof(path) || lstat(path, &info)) continue;
            // The core's VFS walker has no symlink cycle detection.
            if (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)) continue;
            dir.name = item->d_name;
            dir.is_directory = S_ISDIR(info.st_mode);
            return true;
        }
    }
    for (;;) {
        if (dir.next == dir.count) {
            if (!dir.token || dir.finished) return false;
            dir.next = 0;
            if (ps2_sb4_fs_read_dir(dir.token, dir.entries.get(), SB4_STORAGE_PAGE_MAX, &dir.count) ||
                !dir.count || dir.count > SB4_STORAGE_PAGE_MAX) {
                dir.finished = true;
                dir.count = 0;
                return false;
            }
        }
        const Sb4StorageEntry &item = dir.entries[dir.next++];
        if (!visible(item.name, dir.include_hidden)) continue;
        dir.name = item.name;
        dir.is_directory = item.directory != 0;
        return true;
    }
}

const char *entry_name(void *handle) { return static_cast<Directory *>(handle)->name; }
bool entry_is_directory(void *handle) { return static_cast<Directory *>(handle)->is_directory; }

int close_directory(void *handle) {
    std::unique_ptr<Directory> dir(static_cast<Directory *>(handle));
    if (dir->local && closedir(dir->local)) return -1;
    if (dir->token && ps2_sb4_fs_close_dir(dir->token)) return -1;
    --open_directories;
    return 0;
}
}

bool ps2_sb4_vm_fs_install(const char *scratch, const char *image, const char *data_root, bool background_disc_reads,
                           const char *resources) {
    if (!scratch || scratch[0] != '/' || !image || installed || HostVFS::IsInstalled()) return false;
    const bool disc = *image != 0;
    if (disc && (!at_or_under(image, GamesRoot, GamesRootSize) ||
        image[GamesRootSize] != '/' || !image[GamesRootSize + 1])) return false;
    BOOT("08 format storage paths");
    const int length = std::snprintf(bios_root, sizeof(bios_root), "%s/bios", scratch);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(bios_root)) return false;
    size_t resource_length = 0;
    if (resources) {
        while (resource_length < sizeof resources_root && resources[resource_length]) ++resource_length;
    }
    if (resources && (resource_length < 2 || resource_length >= sizeof resources_root || *resources != '/'))
        return false;
    const int card_length = std::snprintf(card_root, sizeof(card_root), "%s/memcards", scratch);
    const bool module_cards = data_root != nullptr;
    BOOT("09 check save directories");
    if (module_cards && (card_length < 0 || static_cast<size_t>(card_length) >= sizeof(card_root) ||
        ps2_sb4_fs_ensure_directory("saves") || ps2_sb4_fs_ensure_directory(PS2_SB4_RUNTIME_DIR) ||
        ps2_sb4_fs_ensure_directory(CardsPath + 1) || ps2_sb4_fs_ensure_directory(StatesPath + 1))) return false;
    int state_length = 0;
    gzip_vfs_root[0] = 0;
    runtime_vfs_root[0] = 0;
    if (data_root) {
        state_length = std::snprintf(state_os_root, sizeof(state_os_root), "%s/sstates", data_root);
        const int runtime_length = std::snprintf(runtime_vfs_root, sizeof(runtime_vfs_root), "%s/data", scratch);
        if (runtime_length <= 0 || static_cast<size_t>(runtime_length) >= sizeof(runtime_vfs_root)) return false;
        const int virtual_length = std::snprintf(state_vfs_root, sizeof(state_vfs_root), "%s/sstates", runtime_vfs_root);
        const int gzip_length = std::snprintf(gzip_vfs_root, sizeof(gzip_vfs_root), "%s/gzip-index", runtime_vfs_root);
        if (state_length <= 0 || static_cast<size_t>(state_length) >= sizeof(state_os_root) ||
            virtual_length <= 0 || static_cast<size_t>(virtual_length) >= sizeof(state_vfs_root) ||
            gzip_length <= 0 || static_cast<size_t>(gzip_length) >= sizeof(gzip_vfs_root)) return false;
    }
    BOOT("10 initialize storage");
    if (ps2_sdk_fs_initialize(&media, ps2_sb4_sd_saves_files())) return false;

    if (disc) {
        BOOT("11 open selected disc");
        Ps2SdkFile probe{};
        if (ps2_sdk_fs_open(&media, image, PS2_FS_READ, &probe) || ps2_sdk_fs_close(&probe)) return false;
    }
    if (module_cards) {
        // GS opens its device-wide shader pack before it opens the title's pack.
        // Create the parents through module storage before the GS worker starts.
        constexpr const char *directories[] = {
            PS2_SB4_CACHE_DIR, PS2_SB4_CACHE_DIR "/gs_packs", PS2_SB4_CACHE_DIR "/gs_packs/_",
        };
        for (const char *directory : directories) {
            const uint32_t result = ps2_sb4_fs_ensure_directory(directory);
            if (result) {
                std::fprintf(stderr, "IZPS2: could not prepare shader cache directory %s (0x%08x)\n", directory, result);
                break;
            }
        }
    }
    Ps2FsMount mounts[5] = {{bios_root, BiosAlias, false}};
    size_t count = 1;
    if (disc) mounts[count++] = {GamesRoot, GamesRoot, false};
    if (module_cards) {
        mounts[count++] = {card_root, CardsPath, true};
        mounts[count++] = {runtime_vfs_root, RuntimePath, true};
    }
    if (resources) mounts[count++] = {resources, ResourcesAlias, false};
    BOOT("12 install file bridge");
    if (!ps2_install_file_bridge(&media, mounts, count)) return false;
    bios_root_size = static_cast<size_t>(length);
    resources_root_size = resource_length;
    if (resources) std::memcpy(resources_root, resources, resource_length + 1);
    card_root_size = module_cards ? static_cast<size_t>(card_length) : 0;
    state_os_size = static_cast<size_t>(state_length);
    BOOT("13 set thread and VFS callbacks");
    state_owner = std::this_thread::get_id();
    has_disc = disc;
    disc_reads_on_worker = background_disc_reads;
    HostVFS::Ops ops = *HostVFS::GetOps();
    ops.stat = stat_path;
    if (module_cards) {
        bridge_open = ops.open;
        ops.open = open_module_file;
        ops.mkdir = mkdir_path;
        ops.remove = remove_path;
        ops.rename = rename_path;
    }
    ops.opendir = open_directory;
    ops.readdir = read_directory;
    ops.dirent_get_name = entry_name;
    ops.dirent_is_dir = entry_is_directory;
    ops.closedir = close_directory;
    ops.read_on_opening_thread = read_on_opening_thread;
    HostVFS::Install(ops);
    installed = true;
    return true;
}

std::FILE *ps2_sb4_vm_fs_open_bios_sidecar(const char *path, const char *mode) {
    if (!installed || !path || !mode || !bios_root_size || !runtime_vfs_root[0] ||
        !at_or_under(path, bios_root, bios_root_size) || path[bios_root_size] != '/') {
        errno = EINVAL;
        return nullptr;
    }
    const std::string_view name(path + bios_root_size + 1);
    if (name.empty() || name.find('/') != name.npos || name.find(char(92)) != name.npos ||
        (!name.ends_with(".nvm") && !name.ends_with(".mec"))) {
        errno = EINVAL;
        return nullptr;
    }

    char writable[PS2_FS_PATH_MAX], sdk_name[PS2_FS_PATH_MAX];
    if (!map_path(path + bios_root_size, FirmwarePath, sdk_name)) {
        errno = EINVAL;
        return nullptr;
    }
    const int length = std::snprintf(writable, sizeof(writable), "%s/firmware/%s",
        runtime_vfs_root, name.data());
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(writable)) {
        errno = ENAMETOOLONG;
        return nullptr;
    }

    if (!std::strcmp(mode, "rb")) {
        Sb4StorageInfo info{};
        const uint32_t result = ps2_sb4_fs_stat(sdk_name, &info);
        if (ps2_sb4_fs_is_not_found(result)) {
            if (auto *legacy = HostVFS::OpenAsCFile(path, mode)) return legacy;
            errno = ENOENT;
            return nullptr;
        }
        if (result || info.directory) {
            errno = EIO;
            return nullptr;
        }
    } else if (mode[0] == 'w' && ps2_sb4_fs_ensure_directory(FirmwarePath)) {
        errno = EIO;
        return nullptr;
    }

    if (auto *stream = HostVFS::OpenAsCFile(writable, mode)) return stream;
    errno = EIO;
    return nullptr;
}

uint32_t ps2_sb4_vm_fs_ensure_vm_directory(const char *path) {
    if (!installed || !path) return PS2_FS_INVALID;
    if (!std::strcmp(path, bios_root)) {
        Sb4StorageInfo info{};
        const uint32_t result = ps2_sb4_fs_stat("bios", &info);
        return result ? result : info.directory ? 0 : PS2_FS_INVALID;
    }

    char mapped[PS2_FS_PATH_MAX];
    if (const char *suffix = runtime_sd_suffix(path)) {
        if (map_path(suffix, RuntimePath, mapped)) return ps2_sb4_fs_ensure_directory(mapped + 1);
    } else if (in_cards(path)) {
        if (card_path(path, mapped)) return ps2_sb4_fs_ensure_directory(mapped + 1);
    }
    return PS2_FS_INVALID;
}

bool ps2_sb4_vm_fs_finish() {
    if (!installed || open_directories || media.opened != media.closed) return false;
    HostVFS::Install({});
    installed = false;
    return !HostVFS::IsInstalled();
}

const char *ps2_sb4_vm_fs_gzip_cache(const char *filename) {
    if (!installed || !has_disc || !gzip_vfs_root[0] || !filename ||
        !at_or_under(filename, GamesRoot, GamesRootSize) || std::this_thread::get_id() != state_owner)
        return nullptr;
    return ps2_sb4_fs_ensure_directory(GzipPath + 1) ? nullptr : gzip_vfs_root;
}

bool ps2_sb4_vm_fs_state_is_scoped(const char *filename) {
    return installed && scoped_state_path(filename) && std::string_view(filename).ends_with(".p2s");
}

static const char *state_target(const char *filename, char (&mapped)[PS2_FS_PATH_MAX]) {
    return ps2_sb4_vm_fs_state_is_scoped(filename) ? map_path(filename + state_os_size, StatesPath, mapped) : nullptr;
}

uint32_t ps2_sb4_vm_fs_prepare_state(const char *filename) {
    char mapped[PS2_FS_PATH_MAX];
    if (!state_target(filename, mapped)) return PS2_FS_INVALID;
    return recover_previous(mapped);
}

uint32_t ps2_sb4_vm_fs_publish_state(const char *filename, bool keep_backup) {
    char mapped[PS2_FS_PATH_MAX];
    if (!state_target(filename, mapped)) return PS2_FS_INVALID;
    const std::string target(mapped);
    const std::string stage = target + Ps2Sb4StateStageSuffix;
    const std::string previous = target + (keep_backup ? KeepBackupSuffix : PreviousSuffix);
    bool staged;
    uint32_t result = exists(stage, staged);
    if (result) return result;
    if (!staged) return PS2_FS_INVALID;
    bool primary;
    result = exists(target, primary);
    if (result) return result;
    if (primary) {
        result = ps2_sb4_fs_rename_save(target.c_str(), previous.c_str());
        if (result) return result;
        result = ps2_sb4_fs_commit_saves();
        if (result) {
            recover_previous(target);
            return result;
        }
    }
    result = ps2_sb4_fs_rename_save(stage.c_str(), target.c_str());
    if (result) {
        if (primary) recover_previous(target);
        return result;
    }
    result = ps2_sb4_fs_commit_saves();
    return result || !primary ? result : recover_previous(target);
}

void ps2_sb4_vm_fs_discard_state_stage(const char *filename) {
    char mapped[PS2_FS_PATH_MAX];
    if (state_target(filename, mapped)) remove_if_present(std::string(mapped) + Ps2Sb4StateStageSuffix);
}

extern "C" void ps2_hbxl_vm_fs_initialize(const char *scratch, const char *resources, const char *image) {
    if (!ps2_sb4_vm_fs_install(scratch, image, nullptr, false, resources))
        ps2_hbxl_vm_fatal("libsb4 media setup", __LINE__);
}

extern "C" void ps2_hbxl_vm_fs_shutdown() {
    if (!ps2_sb4_vm_fs_finish()) ps2_hbxl_vm_fatal("libsb4 media handles still open", __LINE__);
}
