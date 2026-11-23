#include "ps2_sb4_fs.h"
#include "ps2_sb4_layout.h"
#include "ps2_sb4_platform.h"
#include "ps2_sb4_vm_fs.h"
#include "hbxl_vm_frontend.h"
#include "ps2_sd_card.h"
#include "ps2_sd_saves.h"
#include <sb4/native-app.h>
#include "common/Console.h"
#include "common/HostVFS.h"
#include "common/ZipStream.h"
#include "CDVD/GzipIndexFile.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void Log::Writev(LOGLEVEL, ConsoleColors, const char *, va_list) {}
extern "C" void ps2_hbxl_vm_fatal(const char *where, unsigned line) {
    std::fprintf(stderr, "%s at %u\n", where, line);
    std::abort();
}

int FileSystem::FSeek64(std::FILE *file, s64 offset, int whence) { return fseeko(file, offset, whence); }
s64 FileSystem::FTell64(std::FILE *file) { return ftello(file); }
s64 FileSystem::FSize64(std::FILE *file) {
    int64_t size = 0;
    return HostVFS::SizeOfCFile(file, &size) ? size : -1;
}

namespace {
std::filesystem::path root;
IzNativeApp *bound;
unsigned commits, reads, opens, closes, renames;
bool fail_commit;
unsigned fail_commit_at;
std::string fail_rename_from;
const char *fail_open_path;

constexpr uint32_t IoError = 0xf901, CommitError = 0xf902;

auto Path(const char *relative) {
    assert(relative && relative[0] != '/' && std::string_view(relative).find("..") == std::string_view::npos);
    return root / relative;
}
int Descriptor(IzNativeApp *app, uint32_t token) {
    assert(app == bound && token);
    return static_cast<int>(token - 1);
}
uint32_t Size(IzNativeApp *app, uint32_t token, uint64_t *out) {
    struct stat status{};
    if (fstat(Descriptor(app, token), &status)) return IoError;
    *out = status.st_size;
    return 0;
}
uint32_t Open(IzNativeApp *app, const char *name, uint32_t mode, uint32_t *token, uint64_t *size) {
    assert(app == bound);
    if (fail_open_path && !std::strcmp(name, fail_open_path)) return IoError;
    const int fd = open(Path(name).c_str(), mode == SB4_STORAGE_OPEN_READ ? O_RDONLY : O_RDWR);
    if (fd < 0) return errno == ENOENT ? 0x202u : IoError;
    *token = fd + 1;
    uint32_t result = Size(app, *token, size);
    if (result) {
        close(fd);
        *token = 0;
        return result;
    }
    ++opens;
    return 0;
}
uint32_t Close(IzNativeApp *app, uint32_t token) {
    ++closes;
    return close(Descriptor(app, token)) ? IoError : 0;
}
uint32_t Read(IzNativeApp *app, uint32_t token, uint64_t offset, void *data, uint32_t bytes, uint32_t *actual) {
    ++reads;
    const ssize_t count = pread(Descriptor(app, token), data, bytes, offset);
    if (count < 0) return IoError;
    *actual = count;
    return 0;
}
uint32_t Write(IzNativeApp *app, uint32_t token, uint64_t offset, const void *data, uint32_t bytes) {
    return pwrite(Descriptor(app, token), data, bytes, offset) == bytes ? 0 : IoError;
}
uint32_t Resize(IzNativeApp *app, uint32_t token, uint64_t size) {
    return ftruncate(Descriptor(app, token), size) ? IoError : 0;
}
uint32_t Flush(IzNativeApp *app, uint32_t token) {
    return fsync(Descriptor(app, token)) ? IoError : 0;
}
uint32_t Create(IzNativeApp *app, const char *name, uint64_t size) {
    assert(app == bound);
    const int fd = open(Path(name).c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return IoError;
    const int resized = ftruncate(fd, size);
    const int closed = close(fd);
    return resized || closed ? IoError : 0;
}
uint32_t Commit(IzNativeApp *app) {
    assert(app == bound);
    ++commits;
    return fail_commit || commits == fail_commit_at ? CommitError : 0;
}
uint32_t MakeDirectory(IzNativeApp *app, const char *name) {
    assert(app == bound);
    if (!mkdir(Path(name).c_str(), 0700)) return 0;
    return errno == EEXIST ? 0x402u : IoError;
}
uint32_t List(IzNativeApp *app, const char *name, Sb4StorageEntry *entries, uint32_t capacity,
              uint32_t *count) {
    assert(app == bound);
    *count = 0;
    std::error_code error;
    std::filesystem::directory_iterator rows(Path(name), error);
    if (error) return IoError;
    uint32_t total = 0;
    for (auto it = rows; it != std::filesystem::directory_iterator(); ++it) {
        if (total == capacity) return SB4_STORAGE_LIMIT;
        const std::string name = it->path().filename().string();
        auto &entry = entries[total++];
        entry = {};
        assert(name.size() < sizeof(entry.name));
        std::memcpy(entry.name, name.data(), name.size());
        entry.directory = it->is_directory();
    }
    *count = total;
    return 0;
}
uint32_t FileSize(IzNativeApp *app, const char *name, uint64_t *out) {
    assert(app == bound);
    struct stat status{};
    if (stat(Path(name).c_str(), &status)) return errno == ENOENT ? 0x202u : IoError;
    if (!S_ISREG(status.st_mode)) return IoError;
    *out = status.st_size;
    return 0;
}
uint32_t PathStat(IzNativeApp *app, const char *name, Sb4StorageInfo *out) {
    assert(app == bound);
    *out = {};
    struct stat status{};
    if (stat(Path(name).c_str(), &status)) return errno == ENOENT ? 0x202u : IoError;
    if (S_ISDIR(status.st_mode)) out->directory = 1;
    else if (S_ISREG(status.st_mode)) out->size = status.st_size;
    else return IoError;
    return 0;
}
std::filesystem::directory_iterator open_dir;
bool dir_in_use = false;
uint32_t OpenDir(IzNativeApp *app, const char *name, uint32_t *token) {
    assert(app == bound && !dir_in_use);
    std::error_code error;
    open_dir = std::filesystem::directory_iterator(Path(name), error);
    if (error) return IoError;
    dir_in_use = true;
    *token = 1;
    return 0;
}
uint32_t ReadDir(IzNativeApp *app, uint32_t token, Sb4StorageEntry *entries, uint32_t cap, uint32_t *count) {
    assert(app == bound && token == 1 && dir_in_use && cap == SB4_STORAGE_PAGE_MAX);
    *count = 0;
    for (; *count < cap && open_dir != std::filesystem::directory_iterator{}; ++*count, ++open_dir) {
        const auto name = open_dir->path().filename().string();
        assert(name.size() < sizeof(entries[0].name));
        Sb4StorageEntry &entry = entries[*count];
        entry = {};
        std::memcpy(entry.name, name.data(), name.size());
        entry.directory = open_dir->is_directory();
    }
    return 0;
}
uint32_t CloseDir(IzNativeApp *app, uint32_t token) {
    assert(app == bound && token == 1 && dir_in_use);
    open_dir = {};
    dir_in_use = false;
    return 0;
}
uint32_t Rename(IzNativeApp *app, const char *from, const char *to) {
    assert(app == bound);
    if (fail_rename_from == from) return IoError;
    if (link(Path(from).c_str(), Path(to).c_str())) return errno == EEXIST ? 0x402u : IoError;
    ++renames;
    return unlink(Path(from).c_str()) ? IoError : 0;
}
uint32_t Remove(IzNativeApp *app, const char *name) {
    assert(app == bound);
    return unlink(Path(name).c_str()) ? IoError : 0;
}

const Sb4StorageFiles files{
    .version = SB4_STORAGE_VERSION, .bytes = sizeof(Sb4StorageFiles),
    .open = Open, .read = Read, .write = Write, .size = Size, .resize = Resize,
    .flush = Flush, .close = Close, .create = Create,
    .mkdir = MakeDirectory, .commit = Commit, .rename_file = Rename, .remove_file = Remove,
};
const Sb4StorageDirectories directories{OpenDir, ReadDir, CloseDir};
const char *ModuleRelativePath(IzNativeApp *app, const char *path) {
    assert(app == bound);
    constexpr char module[] = "/iz2s/modules/izps2/";
    if (!std::strncmp(path, "sdmc:", 5)) path += 5;
    return std::strncmp(path, module, sizeof(module) - 1) ? nullptr : path + sizeof(module) - 1;
}
const Sb4Storage storage{
    .version = SB4_STORAGE_VERSION, .bytes = sizeof(Sb4Storage),
    .list = List, .file_size = FileSize,
    .files = &files, .flags = SB4_STORAGE_WORKER_READ | SB4_STORAGE_WORKER_WRITE,
    .stat_path = PathStat,
    .directories = &directories,
    .relative_path = ModuleRelativePath,
};
uint32_t ReadPad(uint32_t, uint32_t, Sb4PadSample *out) { *out = {}; return 0; }
uint32_t PublishPs2Cache(IzNativeApp *app, const char *staged, const char *target) {
    const char *from = ModuleRelativePath(app, staged);
    const char *to = ModuleRelativePath(app, target);
    assert(from && to && std::string_view(to).starts_with(PS2_SB4_CACHE_DIR "/"));
    const std::string temp_prefix = std::string(to) + ".tmp";
    assert(std::string_view(from).starts_with(temp_prefix));
    if (fail_rename_from == from || fail_commit) return IoError;
    if (std::rename(Path(from).c_str(), Path(to).c_str())) return IoError;
    return Commit(app);
}
const Sb4Compat compat{
    SB4_COMPAT_VERSION, sizeof(Sb4Compat), SB4_COMPAT_LIBOUNCE, 0,
    [](const char *name) -> Sb4CFunction {
        if (!std::strcmp(name, "sb4ReadPad")) return reinterpret_cast<Sb4CFunction>(ReadPad);
        if (!std::strcmp(name, "sb4PublishPs2Cache")) return reinterpret_cast<Sb4CFunction>(PublishPs2Cache);
        return nullptr;
    },
    [](uint32_t, uint32_t) { return IzServiceFunction(nullptr); },
    [](void **, size_t, size_t) { return IoError; },
    [](void *) {}, []() { return uint64_t(1); }, []() { return uint64_t(1); }, [](uint64_t) {},
    [](const char *) -> Sb4CFunction { return +[]() {}; },
};

void check_split_disc(const Ps2SdkFsApi *api) {
    std::filesystem::create_directories(Path("games/Split"));
    constexpr uint64_t part_bytes = UINT64_C(1) << 31;
    auto part = [](const char *name, uint64_t length, char first, char last) {
        const int fd = open(Path(name).c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(fd >= 0 && !ftruncate(fd, length));
        assert(pwrite(fd, &first, 1, 0) == 1 && pwrite(fd, &last, 1, length - 1) == 1 && !close(fd));
    };
    part("games/Split/example.iso.part000", part_bytes, '0', 'A');
    part("games/Split/example.iso.part001", part_bytes, 'B', 'C');
    part("games/Split/example.iso.part002", 1, 'D', 'D');
    part("games/Split/gap.iso.part000", 1, '0', '0');
    part("games/Split/gap.iso.part002", 1, '2', '2');

    const char *disc = "/sd/iz2s/modules/izps2/games/Split/example.iso";
    Ps2SdkFs fs{};
    Ps2SdkFile file{};
    assert(!ps2_sdk_fs_initialize(&fs, api));
    assert(ps2_sdk_fs_open(&fs, disc, PS2_FS_WRITE, &file) == PS2_FS_READ_ONLY);
    assert(ps2_sdk_fs_open(&fs, "/sd/iz2s/modules/izps2/games/Split/gap.iso", PS2_FS_READ, &file) == PS2_FS_BAD_REPLY);
    const unsigned before_opens = opens, before_closes = closes;
    fail_open_path = "games/Split/example.iso.part001";
    assert(ps2_sdk_fs_open(&fs, disc, PS2_FS_READ, &file) == IoError && !file.opened);
    fail_open_path = nullptr;
    assert(opens == before_opens + 1 && closes == before_closes + 1);
    assert(ps2_sb4_vm_fs_install("/izps2-module", disc));
    assert(HostVFS::ExactSizeOfPath(disc) == static_cast<int64_t>(part_bytes * 2 + 1));
    std::FILE *stream = HostVFS::OpenAsCFile(disc, "rb");
    assert(stream && !ps2_sb4_vm_fs_finish());
    char bytes[4]{};
    assert(!fseeko(stream, part_bytes - 1, SEEK_SET) && std::fread(bytes, 1, 2, stream) == 2);
    assert(std::memcmp(bytes, "AB", 2) == 0);
    assert(!fseeko(stream, part_bytes * 2 - 1, SEEK_SET) && std::fread(bytes, 1, sizeof bytes, stream) == 2);
    assert(std::memcmp(bytes, "CD", 2) == 0 && !std::fclose(stream));
    assert(ps2_sb4_vm_fs_finish());

    part("games/Split/example.iso", 1, 'W', 'W');
    assert(!ps2_sdk_fs_open(&fs, disc, PS2_FS_READ, &file) && file.size == 1);
    size_t actual;
    assert(!ps2_sdk_fs_read(&file, 0, bytes, 1, &actual) && actual == 1 && bytes[0] == 'W');
    assert(!ps2_sdk_fs_close(&file));
    std::puts("PASS: split ISO joins three parts above 4 GiB, detects gaps, rolls back partial opens and prefers an intact ISO");
}

void check_gzip_index(const std::filesystem::path &data_root) {
    constexpr s32 Span = 4 * 1024 * 1024;
    std::filesystem::create_directories(root / "games/Racing Games/PAL");
    const auto fixture = root / "games/Racing Games/PAL/sample.iso.gz";
    const char *disc = "/sd/iz2s/modules/izps2/games/Racing Games/PAL/sample.iso.gz";
    std::vector<unsigned char> decoded(Span * 2 + 65536);
    for (size_t i = 0; i < decoded.size(); ++i) decoded[i] = static_cast<unsigned char>(i * 7 + i / 1024);
    gzFile compressed = gzopen(fixture.c_str(), "wb");
    assert(compressed);
    for (size_t at = 0; at < decoded.size(); at += 65536) {
        assert(gzwrite(compressed, decoded.data() + at, 65536) == 65536);
        assert(!gzflush(compressed, Z_SYNC_FLUSH));
    }
    assert(!gzclose(compressed));
    const auto cache_on_sd = data_root / "gzip-index";
    assert(ps2_sb4_vm_fs_install("/izps2-module", disc, data_root.c_str()));
    assert(!std::filesystem::exists(cache_on_sd));
    const char *directory = ps2_sb4_vm_fs_gzip_cache(disc);
    assert(directory && std::filesystem::is_directory(cache_on_sd) && !HostVFS::OpenAsCFile(disc, "wb"));
    const std::string cache = std::string(directory) + "/sample.pindex";
    std::FILE *source = HostVFS::OpenAsCFile(disc, "rb");
    assert(source);
    Access *fresh = nullptr;
    assert(build_index(source, Span, &fresh) > 1);
    std::FILE *output = HostVFS::OpenAsCFile(cache.c_str(), "wb");
    assert(output && GzipIndexFile::Write(output, *fresh) && !std::fclose(output));
    assert(!std::fclose(source));
    free_index(fresh);
    assert(ps2_sb4_vm_fs_finish());

    assert(ps2_sb4_vm_fs_install("/izps2-module", disc, data_root.c_str()));
    source = HostVFS::OpenAsCFile(disc, "rb");
    auto read_index = [&] {
        std::FILE *input = HostVFS::OpenAsCFile(cache.c_str(), "rb");
        assert(input);
        Access *index = GzipIndexFile::Read(input, FileSystem::FSize64(source), Span);
        assert(!std::fclose(input));
        return index;
    };
    Access *reused = read_index();
    assert(reused && reused->uncompressed_size == static_cast<s64>(decoded.size()));
    Zstate state{};
    unsigned char region[4096];
    const size_t offset = Span * 2 + 123;
    assert(extract(source, reused, offset, region, sizeof(region), &state) == sizeof(region));
    assert(!std::memcmp(region, decoded.data() + offset, sizeof(region)));
    if (state.isValid) assert(!inflateEnd(&state.strm));
    free_index(reused);
    std::filesystem::resize_file(cache_on_sd / "sample.pindex", GzipIndexFile::MagicBytes);
    assert(!read_index() && !std::fclose(source) && ps2_sb4_vm_fs_finish());
    std::puts("PASS: nested GZ random reads through libsb4 with a persisted index; an incomplete index is rejected");
}
}

uint32_t ps2_sb4_vm_fs_ensure_vm_directory(const char *path);

std::FILE *ps2_sb4_vm_fs_open_bios_sidecar(const char *, const char *);

int main(int argc, char **argv) {
    assert(argc == 2);
    root = argv[1];
    for (const char *directory : {"games", "bios"}) std::filesystem::create_directory(root / directory);
    const uint64_t offset = (UINT64_C(1) << 32) + 4096;
    const std::vector<unsigned char> data((UINT64_C(1) << 20) + 17, 0x61);
    const int disc = open(Path("games/large.iso").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(disc >= 0 && pwrite(disc, data.data(), data.size(), offset) == ssize_t(data.size()) && !close(disc));
    std::ofstream(Path("bios/sample.bin"), std::ios::binary).put('B');
    constexpr int64_t bios_size = 4 * 1024 * 1024;
    std::filesystem::resize_file(Path("bios/sample.bin"), bios_size);
    std::ofstream(Path("bios/.hidden"), std::ios::binary).put('H');
    std::filesystem::create_directory(Path("bios/region"));
    std::ofstream(Path("bios/region/another.bin"), std::ios::binary).put('R');
    std::filesystem::create_directories(Path("resources/shaders"));
    std::ofstream(Path("resources/GameIndex.yaml"), std::ios::binary) << "Test";
    std::ofstream(Path("resources/shaders/entry.glsl"), std::ios::binary).put('S');

    IzNativeApp7Storage app{};
    app.base.sb4 = &compat;
    bound = &app.base.base.base.base.base.base.base;
    bound->magic = 0x414e5a49;
    bound->version = SB4_NATIVE_ABI;
    bound->bytes = sizeof(app);
    assert(!ps2_sb4_fs_bind(bound));
    app.storage = &storage;
    const Ps2SdkFsApi *api = ps2_sb4_fs_bind(bound);
    assert(api && !ps2_sb4_fs_bind(bound));
    Sb4StorageInfo info{};
    assert(!ps2_sb4_fs_stat("games/large.iso", &info) && info.size == offset + data.size());
    assert(!ps2_sb4_fs_stat("games", &info) && info.directory);
    assert(!ps2_sb4_fs_stat("/iz2s/modules/izps2/games", &info) && info.directory);
    assert(!ps2_sb4_fs_stat("sdmc:/iz2s/modules/izps2/games/large.iso", &info) &&
           info.size == offset + data.size());
    assert(ps2_sb4_fs_stat("/iz2s/modules/other/games/large.iso", &info) == PS2_FS_INVALID);
    assert(ps2_sb4_fs_stat("games/missing.iso", &info) == 0x202 && !info.size && !info.directory);
    auto old_storage = storage;
    old_storage.bytes = offsetof(Sb4Storage, stat_path);
    app.storage = &old_storage;
    assert(!ps2_sb4_fs_stat("games/large.iso", &info) && info.size == offset + data.size());
    assert(!ps2_sb4_fs_stat("games", &info) && info.directory);
    assert(!ps2_sb4_fs_stat("/iz2s/modules/izps2/games/large.iso", &info) && info.size == offset + data.size());
    assert(ps2_sb4_fs_stat("/izmysh/ps2/games", &info) == PS2_FS_INVALID);
    app.storage = &storage;
    Ps2SdkFs fs{};
    assert(!ps2_sdk_fs_initialize(&fs, api));
    Ps2SdkFile file{};
    assert(ps2_sdk_fs_open(&fs, "/sd/iz2s/modules/izps2/games/large.iso", PS2_FS_WRITE, &file) == PS2_FS_READ_ONLY);
    assert(!ps2_sdk_fs_open(&fs, "/sd/iz2s/modules/izps2/games/large.iso", PS2_FS_READ, &file));
    assert(file.size == offset + data.size() && ps2_sb4_fs_unbind() == PS2_FS_INVALID);
    std::vector<unsigned char> buffer(data.size() + 64);
    size_t actual = 0;
    const unsigned before = reads;
    assert(!ps2_sdk_fs_read(&file, offset, buffer.data(), buffer.size(), &actual));
    assert(reads - before == 2 && actual == data.size() && std::equal(data.begin(), data.end(), buffer.begin()));
    assert(!ps2_sdk_fs_close(&file));
    assert(!ps2_sdk_fs_open(&fs, "sdmc:/iz2s/modules/izps2/games/large.iso", PS2_FS_READ, &file));
    assert(file.size == offset + data.size() && !ps2_sdk_fs_close(&file));
    assert(!ps2_sdk_fs_open(&fs, "/sd/iz2s/modules/izps2/bios/sample.bin", PS2_FS_READ, &file));
    assert(!ps2_sdk_fs_read(&file, 0, buffer.data(), 1, &actual) && actual == 1 && buffer[0] == 'B');
    assert(!ps2_sdk_fs_close(&file));
    assert(ps2_sdk_fs_open(&fs, "/scratch/other/sample.bin", PS2_FS_READ, &file) == PS2_FS_INVALID);
    assert(opens == closes && !ps2_sb4_fs_unbind());

    auto read_only = storage;
    read_only.flags = SB4_STORAGE_WORKER_READ;
    app.storage = &read_only;
    assert(!ps2_sb4_sd_saves_attach(bound));
    app.storage = &storage;
    Ps2Sb4Platform platform{};
    assert(!ps2_sb4_platform_attach(bound, &platform) && !platform.files);
    assert(!ps2_sb4_sd_saves_files());
    IzNativeGraphics graphics{};
    graphics.version = 1;
    graphics.bytes = sizeof(graphics);
    graphics.instance = graphics.physical = graphics.device = 1;
    app.base.base.graphics = &graphics;
    app.base.base.presentation_acquire = [](IzNativeApp *, uint64_t *) { return IoError; };
    app.base.base.presentation_release = [](IzNativeApp *, uint64_t) { return IoError; };
    assert(ps2_sb4_platform_attach(bound, &platform));
    assert(platform.files && platform.vulkan.graphics == &graphics && platform.pad.read == ReadPad);
    assert(ps2_sb4_sd_saves_detach(nullptr) == PS2_FS_INVALID);
    Ps2Sb4Platform duplicate{};
    assert(!ps2_sb4_platform_attach(bound, &duplicate) && !duplicate.files && !duplicate.vulkan.graphics);
    assert(!ps2::SdSavesEnabled());
    const char *game = "/sd/iz2s/modules/izps2/games/large.iso";
    const char *bios = "/scratch-local/bios/sample.bin";
    assert(!ps2_sb4_vm_fs_install("/scratch-local", "/sd/iz2s/modules/izps2/games/missing.iso"));
    ps2_hbxl_vm_fs_initialize("/scratch-local", "/resources", game);
    int64_t scan_size = 0;
    assert(HostVFS::IsInstalled() && HostVFS::StatPath(bios, nullptr, &scan_size) && scan_size == bios_size);
    assert(HostVFS::ExactSizeOfPath(game) == static_cast<int64_t>(offset + data.size()));
    assert(HostVFS::StatPath(game, nullptr, &scan_size) && scan_size == INT32_MAX);
    assert(!HostVFS::StatPath("/scratch-local/bios-extra/sample.bin", nullptr));
    bool is_directory = false;
    assert(HostVFS::StatPath("/scratch-local/bios", &is_directory) && is_directory);
    assert(HostVFS::StatPath("/scratch-local/bios/region", &is_directory) && is_directory);
    assert(!HostVFS::StatPath("/scratch-local/bios/absent", nullptr));
    const auto *vfs = HostVFS::GetOps();
    assert(vfs->read_on_opening_thread(game));
    assert(vfs->read_on_opening_thread(bios));
    assert(!vfs->read_on_opening_thread("/local/disc.iso"));
    assert(!vfs->opendir(bios, false));
    auto list_dir = [&](const char *path, bool include_hidden) {
        auto *dir = vfs->opendir(path, include_hidden);
        assert(dir && !ps2_sb4_vm_fs_finish());
        std::vector<std::string> names;
        while (vfs->readdir(dir)) {
            std::string name = vfs->dirent_get_name(dir);
            assert(name != "." && name != "..");
            names.push_back(name + (vfs->dirent_is_dir(dir) ? "/" : ""));
        }
        assert(!vfs->dirent_get_name(dir) && !vfs->closedir(dir));
        std::sort(names.begin(), names.end());
        return names;
    };
    assert((list_dir("/scratch-local/bios", false) == std::vector<std::string>{"region/","sample.bin"}));
    app.storage = &old_storage;
    assert((list_dir("/scratch-local/bios", true) == std::vector<std::string>{".hidden","region/","sample.bin"}));
    app.storage = &storage;
    assert((list_dir("/scratch-local/bios/region", false) == std::vector<std::string>{"another.bin"}));
    assert((list_dir("/sd/iz2s/modules/izps2/games", false) == std::vector<std::string>{"large.iso"}));
    assert((list_dir(root.c_str(), false) == std::vector<std::string>{"bios/","games/","resources/"}));
    assert(HostVFS::StatPath("/resources", &is_directory) && is_directory);
    assert(HostVFS::ExactSizeOfPath("/resources/GameIndex.yaml") == 4);
    assert((list_dir("/resources/shaders", false) == std::vector<std::string>{"entry.glsl"}));
    assert(!HostVFS::StatPath("/resources-extra/GameIndex.yaml", nullptr));
    assert(!vfs->opendir("/resources/../games", false));
    assert(!HostVFS::OpenAsCFile("/resources/GameIndex.yaml", "r+b"));
    std::FILE *resource_stream = HostVFS::OpenAsCFile("/resources/GameIndex.yaml", "rb");
    char resource_bytes[4];
    assert(resource_stream && !ps2_sb4_vm_fs_finish());
    assert(std::fread(resource_bytes, 1, sizeof resource_bytes, resource_stream) == sizeof resource_bytes &&
           !std::memcmp(resource_bytes,"Test",sizeof resource_bytes) && !std::fclose(resource_stream));
    const auto many = root / "games/many";
    std::filesystem::create_directory(many);
    for (unsigned i = 0; i < SB4_STORAGE_LIST_MAX + 16; ++i)
        std::ofstream(many / ("disc-" + std::to_string(i) + ".iso"));
    assert(list_dir("/sd/iz2s/modules/izps2/games/many", false).size() == SB4_STORAGE_LIST_MAX + 16 && !dir_in_use);
    std::FILE *bios_stream = HostVFS::OpenAsCFile(bios, "rb");
    assert(bios_stream && std::fgetc(bios_stream) == 'B' && !std::fclose(bios_stream));
    assert(!HostVFS::OpenAsCFile(game, "r+b"));
    std::FILE *disc_stream = HostVFS::OpenAsCFile(game, "rb");
    assert(disc_stream && !ps2_sb4_vm_fs_finish());
    int64_t disc_size = 0;
    assert(HostVFS::SizeOfCFile(disc_stream, &disc_size) && disc_size == static_cast<int64_t>(offset + data.size()));
    assert(!fseeko(disc_stream, offset, SEEK_SET) && std::fgetc(disc_stream) == 'a' && !std::fclose(disc_stream));
    ps2_hbxl_vm_fs_shutdown();
    assert(!HostVFS::IsInstalled());
    assert(ps2_sb4_vm_fs_install("/another-scratch", "", nullptr, false, "/another-scratch/resources"));
    assert(HostVFS::StatPath("/another-scratch/bios/sample.bin", nullptr) && !HostVFS::StatPath(game, nullptr));
    assert(HostVFS::ExactSizeOfPath("/another-scratch/resources/GameIndex.yaml") == 4);
    assert(!HostVFS::StatPath("/resources/GameIndex.yaml", nullptr));
    assert(!HostVFS::GetOps()->opendir("/sd/iz2s/modules/izps2/games", false));
    assert(!HostVFS::GetOps()->read_on_opening_thread(game));
    assert(ps2_sb4_vm_fs_finish());

    const char *cards = "/izps2-module/memcards";
    const char *slot1 = "/izps2-module/memcards/slot1.ps2";
    const char *slot2 = "/izps2-module/memcards/slot2.ps2";
    const auto data_root = root / "saves/armsx2";
    const auto device_cache = Path(PS2_SB4_CACHE_DIR "/gs_packs/_");
    assert(!std::filesystem::exists(device_cache));
    assert(ps2_sb4_vm_fs_install("/izps2-module", game, data_root.c_str(), true, "/izps2-module/resources"));
    assert(std::filesystem::is_directory(device_cache));
    vfs = HostVFS::GetOps();
    assert(!vfs->read_on_opening_thread(game));
    assert(vfs->read_on_opening_thread("/izps2-module/bios/sample.bin"));
    assert(vfs->read_on_opening_thread("/izps2-module/resources/GameIndex.yaml"));
    assert(vfs->mkdir("/izps2-module/resources/new") == -1);
    constexpr auto hosted_logs = "sdmc:/iz2s/modules/izps2/saves/armsx2/logs";
    constexpr auto hosted_log = "sdmc:/iz2s/modules/izps2/saves/armsx2/logs/hosted.log";
    assert(!ps2_sb4_vm_fs_ensure_vm_directory("/izps2-module/bios"));
    constexpr const char *module_bios = "/izps2-module/bios/sample.bin";
    bool bios_directory = false;
    assert(HostVFS::StatPath(module_bios, &bios_directory) && !bios_directory);
    auto *module_bios_stream = HostVFS::OpenAsCFile(module_bios, "rb");
    assert(module_bios_stream && !std::fseek(module_bios_stream, 0, SEEK_END) && std::ftell(module_bios_stream) == 4*1024*1024);
    assert(!std::fseek(module_bios_stream, 0, SEEK_SET) && std::fgetc(module_bios_stream) == 'B' && !std::fclose(module_bios_stream));
    assert(!ps2_sb4_vm_fs_ensure_vm_directory("/izps2-module/memcards"));
    assert(ps2_sb4_vm_fs_ensure_vm_directory("/izps2-module/resources") != 0);
    assert(ps2_sb4_vm_fs_ensure_vm_directory("/izps2-module/../outside") != 0);
    assert(!ps2_sb4_vm_fs_ensure_vm_directory(hosted_logs));
    assert(vfs->mkdir(hosted_logs) == 0);
    assert(vfs->mkdir(hosted_logs) == 0);
    bool log_directory = false;
    assert(HostVFS::StatPath(hosted_logs, &log_directory) && log_directory);
    assert(vfs->mkdir("sdmc:/iz2s/modules/izps2/saves/armsx2/../outside") == -1);
    assert(!HostVFS::OpenAsCFile("sdmc:/iz2s/modules/izps2/saves/armsx2/../outside/test", "wb"));
    std::FILE *log = HostVFS::OpenAsCFile(hosted_log, "wb");
    assert(log && std::fwrite("TEST", 1, 4, log) == 4 && !std::fclose(log));
    assert(std::filesystem::file_size(Path("saves/armsx2/logs/hosted.log")) == 4);
    assert(HostVFS::ExactSizeOfPath(hosted_log) == 4);
    constexpr auto cache = "sdmc:/iz2s/modules/izps2/" PS2_SB4_CACHE_DIR;
    constexpr auto active = "sdmc:/iz2s/modules/izps2/" PS2_SB4_CACHE_DIR "/warm.bin";
    constexpr auto staged = "sdmc:/iz2s/modules/izps2/" PS2_SB4_CACHE_DIR "/warm.bin.tmp42";
    auto create = [](const char *name, int value) {
        auto *stream = HostVFS::OpenAsCFile(name, "wb");
        assert(stream && std::fputc(value, stream) == value && !std::fclose(stream));
    };
    auto read = [](const char *name) {
        auto *stream = HostVFS::OpenAsCFile(name, "rb");
        assert(stream);
        const int value = std::fgetc(stream);
        assert(!std::fclose(stream));
        return value;
    };
    const char *legacy_nvm = "/izps2-module/bios/sample.nvm";
    auto sidecar = [&](const char *mode) { return ps2_sb4_vm_fs_open_bios_sidecar(legacy_nvm, mode); };
    assert(!sidecar("rb") && errno == ENOENT);
    auto *legacy = std::fopen(Path("bios/sample.nvm").c_str(), "wb");
    assert(legacy && std::fputc('L', legacy) == 'L' && !std::fclose(legacy));
    auto *rom_sidecar = sidecar("rb");
    assert(rom_sidecar && std::fgetc(rom_sidecar) == 'L' && !std::fclose(rom_sidecar));
    auto *new_sidecar = sidecar("w+b");
    assert(new_sidecar && std::fputc('N', new_sidecar) == 'N' && !std::fclose(new_sidecar));
    rom_sidecar = sidecar("rb");
    assert(rom_sidecar && std::fgetc(rom_sidecar) == 'N' && !std::fclose(rom_sidecar));
    legacy = std::fopen(Path("bios/sample.nvm").c_str(), "rb");
    assert(legacy && std::fgetc(legacy) == 'L' && !std::fclose(legacy));
    assert(!ps2_sb4_vm_fs_open_bios_sidecar("/izps2-module/bios/sample.bin", "wb"));
    assert(!ps2_sb4_vm_fs_open_bios_sidecar("/izps2-module/bios/../sample.nvm", "wb"));
    std::puts("PASS: legacy BIOS settings are read and new settings persist under writable module storage");
    create(active, 'A');
    create(staged, 'B');
    fail_rename_from = PS2_SB4_CACHE_DIR "/warm.bin.tmp42";
    assert(vfs->rename(staged, active) == -1 && read(active) == 'A' && read(staged) == 'B');
    fail_rename_from.clear();
    assert(vfs->rename(staged, "sdmc:/iz2s/modules/izps2/" PS2_SB4_CACHE_DIR "/../outside") == -1);
    assert(vfs->rename(staged, "/izps2-module/bios/sample.bin") == -1);
    assert(!vfs->rename(staged, active) && read(active) == 'B');
    assert(!HostVFS::StatPath(staged, nullptr));
    assert(vfs->rename(staged, active) == -1 && read(active) == 'B');
    auto *directory = vfs->opendir(cache, false);
    assert(directory);
    bool listed = false;
    while (vfs->readdir(directory)) listed |= !std::strcmp(vfs->dirent_get_name(directory), "warm.bin");
    vfs->closedir(directory);
    assert(listed);
    create(staged, 'C');
    const unsigned before_cleanup = commits;
    assert(!vfs->remove(staged) && !HostVFS::StatPath(staged, nullptr) && commits == before_cleanup + 1);
    assert(read(active) == 'B');
    std::puts("PASS: hosted cache publication, failure preservation, enumeration and cleanup");
    constexpr auto backup_dir = "sdmc:/iz2s/modules/izps2/saves/armsx2/memcard-backups";
    constexpr auto part = "sdmc:/iz2s/modules/izps2/saves/armsx2/memcard-backups/card.zip.part";
    constexpr auto archive = "sdmc:/iz2s/modules/izps2/saves/armsx2/memcard-backups/card.zip";
    assert(!vfs->mkdir(backup_dir));
    auto *backup_stage = HostVFS::OpenAsCFile(part, "w+b");
    zip_error_t zip_error;
    zip_error_init(&zip_error);
    auto *zip_output = ZipSourceToStream(backup_stage, &zip_error);
    assert(zip_output);
    auto *zip = zip_open_from_source(zip_output, ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    assert(zip);
    auto *input = zip_source_buffer(zip, "MEMCARD", 7, 0);
    assert(input && zip_file_add(zip, "card/Mcd001.ps2", input, 0) >= 0);
    assert(!zip_close(zip) && !std::fclose(backup_stage) && !vfs->rename(part, archive));
    assert(!HostVFS::StatPath(part, nullptr));
    auto *backup_input = HostVFS::OpenAsCFile(archive, "rb");
    s64 backup_size = 0;
    assert(backup_input && HostVFS::SizeOfCFile(backup_input, &backup_size) && backup_size > 0);
    auto *zip_input = ZipSourceFromStream(backup_input, static_cast<zip_uint64_t>(backup_size), &zip_error);
    assert(zip_input);
    zip = zip_open_from_source(zip_input, ZIP_RDONLY, &zip_error);
    assert(zip);
    auto *entry = zip_fopen(zip, "card/Mcd001.ps2", 0);
    char contents[7];
    assert(entry && zip_fread(entry, contents, sizeof(contents)) == 7);
    assert(!std::memcmp(contents, "MEMCARD", 7));
    assert(!zip_fclose(entry) && !zip_close(zip));
    zip_error_fini(&zip_error);
    std::puts("PASS: staged ZIP backup can be written, published and read through hosted SD");
    assert(ps2_sb4_vm_fs_finish());
    assert(!ps2_sb4_sd_saves_detach(bound));
    std::puts("PASS: hosted directory setup and log I/O use module SD paths");
    return 0;
    assert(vfs->remove("/izps2-module/resources/GameIndex.yaml") == -1);
    assert(vfs->rename("/izps2-module/resources/GameIndex.yaml", slot1) == -1);
    assert(HostVFS::ExactSizeOfPath("/izps2-module/resources/GameIndex.yaml") == 4);
    disc_stream = HostVFS::OpenAsCFile(game, "rb");
    assert(disc_stream);
    std::thread read_disc([&] {
        assert(!fseeko(disc_stream, offset, SEEK_SET) && std::fgetc(disc_stream) == 'a');
    });
    read_disc.join();
    assert(!std::fclose(disc_stream));
    assert(HostVFS::StatPath(cards, &is_directory) && is_directory);
    assert(vfs->read_on_opening_thread(slot1));
    assert(!HostVFS::OpenAsCFile(slot1, "r+b"));
    assert(!HostVFS::OpenAsCFile("/izps2-module/memcards/../outsider", "wb"));
    assert(vfs->mkdir("/izps2-module/memcards/../outsider") != 0);
    std::FILE *card_stream = HostVFS::OpenAsCFile(slot1, "wb");
    assert(card_stream && !ps2_sb4_vm_fs_finish());
    assert(std::fwrite("TEST", 1, 4, card_stream) == 4 && !std::fclose(card_stream));
    assert(HostVFS::ExactSizeOfPath(slot1) == 4);
    assert((list_dir(cards, false) == std::vector<std::string>{"slot1.ps2"}));
    card_stream = HostVFS::OpenAsCFile(slot1, "r+b");
    const unsigned commits_before_card_save = commits;
    assert(card_stream && std::fgetc(card_stream) == 'T');
    assert(!fseeko(card_stream, 1, SEEK_SET) && std::fputc('2', card_stream) == '2' && !std::fflush(card_stream));
    assert(commits > commits_before_card_save);
    assert(std::ifstream(Path("saves/armsx2/memcards/slot1.ps2")).get() == 'T');
    fail_commit = true;
    assert(!fseeko(card_stream, 2, SEEK_SET) && std::fputc('3', card_stream) == '3');
    assert(std::fflush(card_stream) == EOF);
    fail_commit = false;
    assert(!std::fclose(card_stream));
    assert(vfs->rename(slot1, "/izps2-module/bios/slot1.ps2") != 0);
    assert(!vfs->rename(slot1, slot2) && !HostVFS::StatPath(slot1, nullptr));
    card_stream = HostVFS::OpenAsCFile(slot2, "rb");
    assert(card_stream && std::fgetc(card_stream) == 'T' && std::fgetc(card_stream) == '2');
    assert(!std::fclose(card_stream) && !vfs->remove(slot2));
    assert(!HostVFS::StatPath(slot2, nullptr));

    const auto state = data_root / "sstates/SLUS-00000.01.p2s";
    std::filesystem::create_directory(state.parent_path());
    std::ofstream(state, std::ios::binary).write("PK\x03\x04", 4);
    assert(vfs->read_on_opening_thread(state.c_str()));
    assert(!vfs->read_on_opening_thread((data_root / "sstates-extra/slot.p2s").c_str()));
    assert(!HostVFS::OpenAsCFile(state.c_str(), "r+b"));
    std::thread thumbnail_worker([&] {
        assert(!vfs->read_on_opening_thread(state.c_str()));
        assert(!HostVFS::OpenAsCFile(state.c_str(), "rb"));
        assert(std::ifstream(state, std::ios::binary).get() == 'P');
    });
    thumbnail_worker.join();
    std::FILE *state_stream = HostVFS::OpenAsCFile(state.c_str(), "rb");
    assert(state_stream && !ps2_sb4_vm_fs_finish() && std::fgetc(state_stream) == 'P');
    assert(HostVFS::SizeOfCFile(state_stream, &scan_size) && scan_size == 4);
    assert(!std::fclose(state_stream));
    const std::string stage = state.string() + Ps2Sb4StateStageSuffix;
    const std::string backup = state.string() + ".backup";
    const std::string previous = state.string() + ".iz2s-previous";
    const auto stage_byte = [&](int byte) {
        assert(!ps2_sb4_vm_fs_prepare_state(state.c_str()));
        std::FILE *output = HostVFS::OpenAsCFile(stage.c_str(), "wb");
        assert(output && std::fputc(byte, output) == byte && !std::fclose(output));
    };
    auto get = [](const auto &path) {
        return std::ifstream(path, std::ios::binary).get();
    };
    assert(ps2_sb4_vm_fs_publish_state(state.c_str(), false) == PS2_FS_INVALID && get(state) == 'P');
    stage_byte('A');
    assert(!ps2_sb4_vm_fs_publish_state(state.c_str(), true));
    assert(get(state) == 'A' && get(backup) == 'P' && !std::filesystem::exists(stage));
    stage_byte('B');
    assert(!ps2_sb4_vm_fs_publish_state(state.c_str(), true));
    assert(get(state) == 'B' && get(backup) == 'A');

    stage_byte('C');
    fail_rename_from = "saves/armsx2/sstates/SLUS-00000.01.p2s" + std::string(Ps2Sb4StateStageSuffix);
    assert(ps2_sb4_vm_fs_publish_state(state.c_str(), false) == IoError);
    assert(get(state) == 'B' && get(backup) == 'A');
    fail_rename_from.clear();
    ps2_sb4_vm_fs_discard_state_stage(state.c_str());
    assert(!std::filesystem::exists(stage));

    std::filesystem::rename(state, previous);
    assert(HostVFS::StatPath(state.c_str(), nullptr));
    assert(get(state) == 'B' && !std::filesystem::exists(previous));
    stage_byte('D');
    fail_commit_at = commits + 2;
    assert(ps2_sb4_vm_fs_publish_state(state.c_str(), false) == CommitError);
    assert(get(state) == 'D' && get(previous) == 'B');
    assert(!ps2_sb4_vm_fs_prepare_state(state.c_str()));
    assert(get(state) == 'D' && !std::filesystem::exists(previous) && get(backup) == 'A');
    assert(!vfs->remove(state.c_str()) && !HostVFS::StatPath(state.c_str(), nullptr));
    assert(ps2_sb4_vm_fs_finish());
    std::filesystem::remove(device_cache);
    std::ofstream(device_cache).put('X');
    assert(ps2_sb4_vm_fs_install("/izps2-module", game, data_root.c_str()));
    assert(std::filesystem::is_regular_file(device_cache) && get(device_cache) == 'X');
    assert(ps2_sb4_vm_fs_finish());
    std::filesystem::remove(device_cache);
    check_gzip_index(data_root);
    assert(std::filesystem::is_directory(device_cache));
    check_split_disc(api);

    assert(!ps2_sdk_fs_initialize(&fs, platform.files));
    assert(!ps2_sdk_fs_open(&fs, "/sd/iz2s/modules/izps2/bios/sample.bin", PS2_FS_READ, &file));
    assert(!ps2_sdk_fs_read(&file, 0, buffer.data(), 1, &actual) && actual == 1 && buffer[0] == 'B');
    assert(!ps2_sdk_fs_close(&file));
    assert(!ps2_sdk_fs_open(&fs, "/sd/iz2s/modules/izps2/games/large.iso", PS2_FS_READ, &file));
    assert(file.size == offset + data.size());
    assert(ps2_sd_saves_native_initialize() && ps2_sd_saves_native_initialize());
    assert(!ps2_sdk_fs_read(&file, offset, buffer.data(), 1, &actual) && actual == 1 && buffer[0] == 'a');
    assert(!ps2_sdk_fs_close(&file));
    const auto source = root / "local-card.ps2";
    const auto target = root / "restored-card.ps2";
    {
        std::ofstream out(source, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), data.size());
        assert(out.good());
    }
    const auto first = ps2::StoreSdSave(source.string(), "card");
    assert(first.success && first.generation == 1 && commits);
    std::fstream(source, std::ios::in | std::ios::out | std::ios::binary).put('X');
    const unsigned commits_before_failure = commits;
    fail_commit = true;
    assert(!ps2::StoreSdSave(source.string(), "card").success);
    fail_commit = false;
    assert(commits > commits_before_failure);
    const auto recovered = ps2::RestoreRequiredSdSave(target.string(), "card");
    assert(recovered.success && recovered.generation == 1);
    std::ifstream restored(target, std::ios::binary);
    std::vector<unsigned char> saved{std::istreambuf_iterator<char>(restored), {}};
    assert(saved == data);

    const auto raw = root / "raw-card.ps2";
    constexpr off_t card_bytes = 8 * 1024 * 528 * 2;
    const int fd = open(raw.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(fd >= 0 && !ftruncate(fd, card_bytes) && pwrite(fd, "A", 1, 512) == 1 && !close(fd));
    const unsigned prior_card_renames = renames;
    assert(ps2::CreateLiveSdCard(raw.string(), "slot1.ps2").success && renames == prior_card_renames + 1);
    assert(ps2::CreateLiveSdCard(raw.string(), "slot1.ps2").success && renames == prior_card_renames + 1);
    assert(ps2::BindLiveSdCard(raw.string(), "slot1.ps2"));
    const auto &card = ps2::LiveSdCardBridgeApi();
    uint64_t token = 0;
    assert(!card.open(&token, raw.c_str(), 3));
    uint32_t flush = 1;
    assert(!card.write(token, 512, "B", 1, &flush));
    assert(!card.read(&actual, token, 512, buffer.data(), 1) && actual == 1 && buffer[0] == 'B');
    card.close(token);
    assert(ps2::LiveSdCardHealthy());
    ps2_sd_saves_native_shutdown();
    assert(!ps2::SdSavesEnabled() && !ps2_sb4_sd_saves_files() && opens == closes);
    assert(!ps2_sb4_sd_saves_detach(bound));
    assert(ps2_sb4_platform_attach(bound, &platform) && ps2_sd_saves_native_initialize());
    assert(ps2::RestoreRequiredSdSave(target.string(), "card").generation == 1);
    ps2_sd_saves_native_shutdown();
    assert(!ps2::SdSavesEnabled() && opens == closes);
    std::puts("PASS: scoped save-state replacement, backup rotation, rollback, recovery, and error reporting");
}

extern "C" void izps2_boot_stage(IzNativeApp *, const char *, uint32_t) {}
