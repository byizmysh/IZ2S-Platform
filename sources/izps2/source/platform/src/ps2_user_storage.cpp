#include "ps2_user_storage.h"

#include "SIO/Memcard/MemoryCardFile.h"
#include "common/Path.h"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace ps2 {
namespace {
constexpr off_t Card8MiB = 8 * 1024 * 528 * 2;

bool Text(const std::string& value, size_t limit) {
    if (value.empty() || value.size() > limit)
        return false;
    for (const unsigned char c : value)
        if (c < 32 || c == 127 || c == '\\')
            return false;
    return true;
}

bool RawCardSize(off_t size) {
    return size == Card8MiB || size == Card8MiB * 2 ||
           size == Card8MiB * 4 || size == Card8MiB * 8;
}

bool ExistingCard(const std::string& path, bool& exists, std::string& error) {
    exists = false;
    struct stat st{};
    if (::lstat(path.c_str(), &st)) {
        if (errno == ENOENT)
            return true;
        error = "Cannot inspect private memory card";
        return false;
    }
    exists = true;
    if (!S_ISREG(st.st_mode) || st.st_nlink != 1 || !RawCardSize(st.st_size)) {
        error = "Existing memory card is not a private raw PS2 card of supported size; it was preserved";
        return false;
    }
    return true;
}

// Some ordinary FAT-like host filesystems do not implement fsync. A complete
// logical write can still succeed there, with durability explicitly unproven.
bool Flush(int fd, bool& durable) {
    if (::fsync(fd) == 0)
        return true;
    durable = false;
    return errno == EINVAL || errno == ENOSYS || errno == ENOTSUP || errno == EOPNOTSUPP;
}

bool SyncDirectory(const std::string& directory) {
    const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool synced = ::fsync(fd) == 0;
    return ::close(fd) == 0 && synced;
}

struct Temporary {
    std::string path;
    dev_t device{};
    ino_t inode{};
    bool Remove() {
        if (path.empty())
            return true;
        struct stat st{};
        if (::lstat(path.c_str(), &st))
            return errno == ENOENT;
        if (!S_ISREG(st.st_mode) || st.st_dev != device || st.st_ino != inode)
            return false;
        if (::unlink(path.c_str()))
            return false;
        path.clear();
        return true;
    }
    ~Temporary() { Remove(); }
};
} // namespace

PrivateCardResult PreparePrivateSlot1Card(const std::string& scratch, const std::string& filename) {
    PrivateCardResult result;
    if (!Text(scratch, 256) || !Path::IsAbsolute(scratch) || !Text(filename, 80) ||
        filename.find('/') != std::string::npos || filename.size() < 5 ||
        filename.compare(filename.size() - 4, 4, ".ps2") != 0) {
        result.error = "A private scratch directory and direct .ps2 card name are required";
        return result;
    }
    const std::string root = Path::RealPath(scratch);
    if (root.empty() || root == "/" || root != Path::RealPath(EmuFolders::DataRoot)) {
        result.error = "Memory card scratch must be the VM's private DataRoot";
        return result;
    }
    const std::string directory = EmuFolders::MemoryCards;
    const std::string prefix = root + "/";
    if (directory.compare(0, prefix.size(), prefix) ||
        directory.size() == prefix.size() || directory.find('/', prefix.size()) != std::string::npos ||
        directory.substr(prefix.size()) == "." || directory.substr(prefix.size()) == "..") {
        result.error = "MemoryCards must be a direct private directory under DataRoot";
        return result;
    }
    struct stat st{};
    bool directory_created = false;
    if (::lstat(directory.c_str(), &st)) {
        if (errno != ENOENT) {
            result.error = "Cannot create private memory card directory";
            return result;
        }
        if (::mkdir(directory.c_str(), 0700) == 0)
            directory_created = true;
        else if (errno != EEXIST) {
            result.error = "Cannot create private memory card directory";
            return result;
        }
    }
    if (::lstat(directory.c_str(), &st) || !S_ISDIR(st.st_mode) || Path::RealPath(directory) != directory) {
        result.error = "MemoryCards must be an existing private directory, not a symbolic link";
        return result;
    }
    result.filename = filename;
    result.path = directory + "/" + filename;
    bool exists = false;
    if (!ExistingCard(result.path, exists, result.error))
        return result;
    if (exists) {
        result.success = true;
        return result;
    }

    std::string pattern = directory + "/." + filename + ".new.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    const int reserved = ::mkstemp(name.data());
    if (reserved < 0) {
        result.error = "Cannot reserve private memory card temporary file";
        return result;
    }
    Temporary temporary{std::string(name.data())};
    const bool identified = ::fstat(reserved, &st) == 0;
    if (identified) {
        temporary.device = st.st_dev;
        temporary.inode = st.st_ino;
    }
    const bool closed = ::close(reserved) == 0;
    if (!identified || !closed) {
        result.error = "Cannot identify private card temporary file";
        return result;
    }
    const std::string temp_name = temporary.path.substr(directory.size() + 1);
    if (!FileMcd_CreateNewCard(temp_name, MemoryCardType::File, MemoryCardFileType::PS2_8MB)) {
        result.error = "Original PS2 memory card creation failed";
        return result;
    }
    const int input = ::open(temporary.path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (input < 0) {
        result.error = "Cannot inspect completed private card";
        return result;
    }
    bool durable = true;
    const bool complete = ::fstat(input, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1 &&
        st.st_dev == temporary.device && st.st_ino == temporary.inode && st.st_size == Card8MiB && Flush(input, durable);
    if (!complete) {
        ::close(input);
        result.error = "Original card creation did not complete a private 8MiB raw card";
        return result;
    }

    if (::link(temporary.path.c_str(), result.path.c_str()) == 0) {
        const bool input_closed = ::close(input) == 0;
        result.created = true;
        result.atomic_creation = true;
        if (!temporary.Remove() || !input_closed) {
            result.error = "Card was created, but its private temporary file could not be released";
            return result;
        }
        result.success = true;
        result.durability_confirmed = SyncDirectory(directory) &&
            (!directory_created || SyncDirectory(root)) && durable;
        return result;
    }

    // No hardlink support is required. O_EXCL never opens an existing user
    // card for writing. A failed new copy remains for explicit user recovery.
    const int output = ::open(result.path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (output < 0) {
        const int reason = errno;
        ::close(input);
        if (reason == EEXIST && ExistingCard(result.path, exists, result.error) && exists) {
            result.success = true;
            return result;
        }
        if (result.error.empty())
            result.error = "Cannot exclusively create private memory card; existing files were preserved";
        return result;
    }
    result.created = true;
    bool copied = true;
    off_t total = 0;
    char buffer[65536];
    while (total < Card8MiB) {
        const ssize_t count = ::read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0 || count > Card8MiB - total) { copied = false; break; }
        ssize_t offset = 0;
        while (offset < count) {
            const ssize_t written = ::write(output, buffer + offset, count - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) { copied = false; break; }
            offset += written;
        }
        if (!copied)
            break;
        total += count;
    }
    if (copied)
        copied = total == Card8MiB && ::fstat(output, &st) == 0 && st.st_size == Card8MiB && Flush(output, durable);
    if (::close(input))
        copied = false;
    if (::close(output))
        copied = false;
    if (!copied) {
        result.error = "New memory card copy did not complete; its partial file was preserved";
        return result;
    }
    result.success = true;
    result.durability_confirmed = SyncDirectory(directory) &&
        (!directory_created || SyncDirectory(root)) && durable;
    return result;
}
} // namespace ps2
