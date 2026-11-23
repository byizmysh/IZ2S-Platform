#ifndef PS2_SD_SAVES_H
#define PS2_SD_SAVES_H
#include "sdk_fs.h"
#include <cstdint>
#include <string>

namespace ps2 {
struct SdSaveResult {
    bool success = false, found = false, recovered_previous = false;
    uint64_t generation = 0, bytes = 0;
    std::string error;
};
// The API must refer to an already mounted SD filesystem. The caller creates
// and owns root; no FILE/libc object crosses the SDK boundary. One CPU owner
// calls these functions with guest/card writers paused.
bool ConfigureSdSaves(const Ps2SdkFsApi&, const std::string& root);
bool SdSavesEnabled();
bool ShutdownSdSaves();
SdSaveResult StoreSdSave(const std::string& local, const std::string& key);
// The caller pins a quiescent source for the complete call. Each callback must
// return zero and exactly bytes in *actual; short/error/throwing reads never
// publish a valid new header. The callback must not reenter this snapshot API.
using SdSaveReadAt = uint32_t (*)(void* opaque, uint64_t offset, void* buffer,
                                size_t bytes, size_t* actual);
SdSaveResult StoreSdSaveFromReader(const std::string& key, uint64_t bytes,
                                 SdSaveReadAt read_at, void* opaque);
// Missing slots are a successful first launch. Corrupt slots never replace a
// local save. Existing different local data is backed up before publication.
SdSaveResult RestoreSdSave(const std::string& local, const std::string& key);
// Explicit/manual loads require an SD generation. Unlike first-start restore,
// missing is failure and cannot authorize loading an older local file.
SdSaveResult RestoreRequiredSdSave(const std::string& local, const std::string& key);
}

// Native binder is separate from this ordinary storage implementation.
// 1 = mounted and configured, 0 = unavailable (error is printed by binder).
extern "C" int ps2_sd_saves_native_initialize();
extern "C" void ps2_sd_saves_native_shutdown();
#endif
