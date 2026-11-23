// SPDX-License-Identifier: GPL-3.0+
#include "nfs-spirv-snapshot.h"

namespace NfsSpirvCache
{
namespace
{
constexpr auto CacheKind = static_cast<GSCacheFile::Kind>(0x534e5a49); // IZNS
constexpr size_t FramingAllowance = 128;
std::atomic<bool> enabled{false};
std::mutex mutex;
NfsSpirvSnapshot::Snapshot shaders;
unsigned hits;
unsigned added;

std::string CachePath()
{
    return Path::Combine(EmuFolders::Cache, "izps2-nfs-spirv-v1.bin");
}

GSCacheFile::Stamp CacheStamp()
{
    GSCacheFile::Stamp stamp;
    stamp.Add("disc", "SLUS-21065/F5C7B45F");
    stamp.Add("format", 1u);
    stamp.Add("shaderc_archive_sha256", IZPS2_SHADERC_ARCHIVE_SHA);
    stamp.Add("shader_policy_sha256", IZPS2_SHADER_POLICY_SHA);
    return stamp;
}

std::optional<NfsSpirvSnapshot::Words> Find(const NfsSpirvSnapshot::Key& key)
{
    if (!enabled.load(std::memory_order_acquire) || GSConfig.UseDebugDevice)
        return std::nullopt;
    const std::lock_guard lock(mutex);
    if (const auto* words = shaders.Find(key))
    {
        ++hits;
        return *words;
    }
    return std::nullopt;
}

void Record(const NfsSpirvSnapshot::Key& key, const NfsSpirvSnapshot::Words& words)
{
    if (!enabled.load(std::memory_order_acquire) || GSConfig.UseDebugDevice)
        return;
    const std::lock_guard lock(mutex);
    if (shaders.Insert(key, words))
        ++added;
}
}

void Reset()
{
    enabled.store(false, std::memory_order_release);
    const std::lock_guard lock(mutex);
    shaders = {};
    hits = 0;
    added = 0;
}

void Begin()
{
    const std::string path = CachePath();
    const auto size = FileSystem::GetPathFileSize(path.c_str());
    auto result = GSCacheFile::ReadResult::Missing;
    std::vector<uint8_t> payload;
    if (size > 0 && static_cast<size_t>(size) <= NfsSpirvSnapshot::MaxPayload + FramingAllowance)
        result = GSCacheFile::ReadFramedFile(path, CacheKind, CacheStamp(), &payload);
    const std::lock_guard lock(mutex);
    const bool loaded = result == GSCacheFile::ReadResult::Ok && shaders.Load(payload);
    Console.WriteLn("IZPS2_NFS_SPIRV startup status=%s records=%zu bytes=%zu",
        loaded ? "loaded" : size > 0 ? "rejected" : "missing", shaders.Size(), payload.size());
    enabled.store(true, std::memory_order_release);
}

void ReportAndSave(bool allow_write)
{
    if (!enabled.exchange(false, std::memory_order_acq_rel))
        return;
    const std::lock_guard lock(mutex);
    bool saved = false;
    size_t bytes = 0;
    if (allow_write && added)
    {
        const auto payload = shaders.Serialize();
        bytes = payload.size();
        saved = GSCacheFile::WriteFramedFile(CachePath(), CacheKind, CacheStamp(), payload.data(), bytes,
            GSCacheFile::Clock::now() + std::chrono::milliseconds(750));
    }
    Console.WriteLn("IZPS2_NFS_SPIRV finish hits=%u recorded=%u records=%zu bytes=%zu saved=%u",
        hits, added, shaders.Size(), bytes, unsigned(saved));
}
}
