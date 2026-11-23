#pragma once

#include <cerrno>
#include <cstdint>
#include <limits>
#include <sys/types.h>
#include <unistd.h>

// Shared by the ordinary POSIX and mixed SD adapters. Short writes are legal,
// and a signal does not turn a complete card/checkpoint write into an I/O error.
// A hard error can still leave a partial write; no rollback is promised here.
namespace ps2::posix {
inline ssize_t ReadAt(int fd, void* data, size_t bytes, int64_t offset) {
    ssize_t result;
    do {
        result = ::pread(fd, data, bytes, offset);
    } while (result < 0 && errno == EINTR);
    return result;
}

inline int Sync(int fd) {
    int result;
    do {
        result = ::fsync(fd);
    } while (result < 0 && errno == EINTR);
    return result;
}

inline int Resize(int fd, int64_t bytes) {
    int result;
    do {
        result = ::ftruncate(fd, bytes);
    } while (result < 0 && errno == EINTR);
    return result;
}

inline bool WriteAtAndSync(int fd, const void* data, size_t bytes, int64_t offset) {
    if (offset < 0 || bytes > static_cast<uint64_t>(INT64_MAX - offset) || (!data && bytes)) {
        errno = EINVAL;
        return false;
    }
    const auto* input = static_cast<const unsigned char*>(data);
    size_t completed = 0;
    while (completed < bytes) {
        const size_t remaining = bytes - completed;
        const size_t maximum = static_cast<size_t>(std::numeric_limits<ssize_t>::max());
        const size_t chunk = remaining < maximum ? remaining : maximum;
        const ssize_t count = ::pwrite(fd, input + completed, chunk, offset + completed);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0 || static_cast<size_t>(count) > chunk) {
            if (!count)
                errno = EIO;
            return false;
        }
        completed += static_cast<size_t>(count);
    }
    return Sync(fd) == 0;
}
} // namespace ps2::posix
