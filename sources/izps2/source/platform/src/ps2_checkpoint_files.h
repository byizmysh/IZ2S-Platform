#ifndef PS2_CHECKPOINT_FILES_H
#define PS2_CHECKPOINT_FILES_H

// Ordinary private-file publication only. The caller serializes checkpoint
// writers and supplies one already-resolved private directory. This helper
// never creates or changes a source BIOS, disc, card or other media file.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ps2::checkpoint_files {
struct Publication {
    bool success = false;
    bool backup_updated = false;
    bool durability_confirmed = false;
    std::string error;
};

inline bool Name(const std::string& name) {
    if (name.empty() || name == "." || name == "..")
        return false;
    for (const unsigned char byte : name)
        if (byte < 32 || byte == 127 || byte == '/' || byte == '\\')
            return false;
    return true;
}
inline bool Regular(const std::string& path, bool missing_ok, bool nonempty,
                    bool& exists, std::string& error) {
    struct stat st{};
    exists = false;
    if (::lstat(path.c_str(), &st)) {
        if (missing_ok && errno == ENOENT)
            return true;
        error = "Cannot inspect checkpoint file: " + path;
        return false;
    }
    if (!S_ISREG(st.st_mode) || st.st_nlink != 1 || (nonempty && st.st_size <= 0)) {
        error = "Checkpoint must be a private regular nonempty file: " + path;
        return false;
    }
    exists = true;
    return true;
}
inline bool SyncDirectory(const std::string& root) {
    const int fd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool ok = ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    return ok && closed;
}
inline bool FlushFile(int fd, bool& durable) {
    if (::fsync(fd) == 0)
        return true;
    durable = false;
    // A complete logical write remains usable on an ordinary filesystem that
    // does not implement fsync. Actual I/O errors must still prevent publication.
    return errno == EINVAL || errno == ENOSYS || errno == ENOTSUP || errno == EOPNOTSUPP;
}
inline bool SyncFile(const std::string& path, std::string& error, bool& durable) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        error = "Cannot open completed checkpoint for flush";
        return false;
    }
    struct stat st{};
    const bool valid = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_size > 0;
    const bool synced = valid && FlushFile(fd, durable);
    const bool closed = ::close(fd) == 0;
    if (!synced || !closed)
        error = "Cannot flush completed checkpoint";
    return synced && closed;
}
inline bool CopyBackup(const std::string& current, const std::string& backup,
                       std::string& error, bool& durable) {
    const int input = ::open(current.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (input < 0) {
        error = "Cannot open previous checkpoint for backup";
        return false;
    }
    struct stat st{};
    if (::fstat(input, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 || st.st_size <= 0) {
        ::close(input);
        error = "Previous checkpoint is not a private regular nonempty file";
        return false;
    }
    std::string pattern = backup + ".part.XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end());temp.push_back(0);
    const int output = ::mkstemp(temp.data());
    if (output < 0) {
        ::close(input);
        error = "Cannot create a private backup temporary file";
        return false;
    }
    bool ok = true;
    char buffer[65536];
    for (;;) {
        ssize_t count = ::read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) { ok = false; break; }
        if (!count)
            break;
        ssize_t offset = 0;
        while (offset < count) {
            const ssize_t written = ::write(output, buffer + offset, count - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) { ok = false; break; }
            offset += written;
        }
        if (!ok)
            break;
    }
    if (ok && !FlushFile(output, durable))
        ok = false;
    if (::close(input))
        ok = false;
    if (::close(output))
        ok = false;
    if (ok && ::rename(temp.data(), backup.c_str()) == 0)
        return true;
    ::unlink(temp.data());
    error = "Cannot publish previous checkpoint as backup";
    return false;
}

// The complete staging file is flushed first. Previous current is copied and
// atomically published as backup without removing current. Only then does one
// same-directory rename replace current. Failure before that final rename
// leaves current intact. A crash can leave an ignored .part or staging file;
// lookup must only consider current/backup, never those unfinished candidates.
// Directory fsync availability is reported separately from logical success.
inline Publication Publish(const std::string& root, const std::string& current_name,
                           const std::string& backup_name, const std::string& staging_name,
                           bool backup_current = true) {
    Publication result;
    if (!Name(current_name) || !Name(backup_name) || !Name(staging_name) ||
        current_name == backup_name || current_name == staging_name || backup_name == staging_name) {
        result.error = "Distinct private checkpoint basenames are required";
        return result;
    }
    const std::string current = root + "/" + current_name;
    const std::string backup = root + "/" + backup_name;
    const std::string staging = root + "/" + staging_name;
    bool has_current = false, has_backup = false, has_staging = false;
    bool durable = true;
    if (!Regular(current, true, true, has_current, result.error) ||
        !Regular(backup, true, true, has_backup, result.error) ||
        !Regular(staging, false, true, has_staging, result.error) ||
        !SyncFile(staging, result.error, durable))
        return result;
    if (has_current && backup_current) {
        if (!CopyBackup(current, backup, result.error, durable))
            return result;
        result.backup_updated = true;
        durable = SyncDirectory(root) && durable;
    }
    if (::rename(staging.c_str(), current.c_str())) {
        result.error = "Cannot publish completed autosave; previous current and backup are retained";
        return result;
    }
    result.success = true;
    result.durability_confirmed = SyncDirectory(root) && durable;
    return result;
}
} // namespace ps2::checkpoint_files
#endif
