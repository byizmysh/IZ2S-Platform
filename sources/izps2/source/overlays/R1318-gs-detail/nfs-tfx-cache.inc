// SPDX-License-Identifier: GPL-3.0+
#include "nfs-tfx-snapshot.h"

namespace NfsTfx
{
namespace
{
constexpr auto CacheKind = static_cast<GSCacheFile::Kind>(0x5846544e); // NTFX
constexpr double WarmBudgetMs = 100.0;
std::mutex s_mutex;
std::atomic<bool> s_armed{false};
std::atomic<const void*> s_owner{nullptr};
std::atomic<const uint32_t*> s_stop_requested{nullptr};
Snapshot s_snapshot;
bool s_enabled = false;
bool s_dirty = false;
size_t s_collected = 0;
struct WarmupStats
{
    size_t keys;
    unsigned created;
    unsigned rejected;
    unsigned already;
    bool budget_stop;
    bool failed;
    double elapsed_ms;
};
std::optional<WarmupStats> s_warmup;

std::string PathOnSd() { return Path::Combine(EmuFolders::Cache, "izps2-nfs-tfx-v1.bin"); }

GSCacheFile::Stamp Stamp()
{
    GSCacheFile::Stamp stamp;
    stamp.Add("disc", "SLUS-21065/F5C7B45F");
    stamp.Add("selector-policy", "r1304-canonical-1");
    stamp.Add("format", 1u);
    return stamp;
}

std::vector<Key> Select(const void* owner, const Fingerprint& fingerprint)
{
    const std::lock_guard lock(s_mutex);
    if (s_snapshot.fingerprint != fingerprint)
        s_snapshot = {fingerprint, {}};
    s_owner.store(owner, std::memory_order_release);
    return s_snapshot.keys;
}

void Remember(const void* owner, const Key& key)
{
    if (s_owner.load(std::memory_order_acquire) != owner)
        return;
    const std::lock_guard lock(s_mutex);
    if (s_snapshot.Insert(key))
    {
        s_dirty = true;
        ++s_collected;
    }
}

void Detach(const void* owner)
{
    s_owner.compare_exchange_strong(owner, nullptr, std::memory_order_acq_rel);
}

bool Stopping()
{
    const auto* stop_requested = s_stop_requested.load(std::memory_order_acquire);
    return stop_requested && __atomic_load_n(stop_requested, __ATOMIC_ACQUIRE) != 0;
}

void SetWarmup(WarmupStats stats)
{
    const std::lock_guard lock(s_mutex);
    s_warmup = stats;
}
}

void Reset()
{
    s_armed.store(false, std::memory_order_release);
    s_owner.store(nullptr, std::memory_order_release);
    s_stop_requested.store(nullptr, std::memory_order_release);
    const std::lock_guard lock(s_mutex);
    s_snapshot = {};
    s_warmup.reset();
    s_enabled = false;
    s_dirty = false;
    s_collected = 0;
}

void Arm(const uint32_t* stop_requested)
{
    std::vector<uint8_t> data;
    const auto path = PathOnSd();
    const auto bytes = FileSystem::GetPathFileSize(path.c_str());
    if (bytes > 0 && static_cast<size_t>(bytes) <= MaxPayload + 128 &&
        GSCacheFile::ReadFramedFile(path, CacheKind, Stamp(), &data) != GSCacheFile::ReadResult::Ok)
        data.clear();
    const std::lock_guard lock(s_mutex);
    const bool loaded = s_snapshot.Load(data);
    s_enabled = true;
    s_stop_requested.store(stop_requested, std::memory_order_release);
    s_armed.store(true, std::memory_order_release);
    Console.WriteLn("IZPS2_NFS_TFX_STORE status=%s keys=%zu", loaded ? "loaded" : bytes > 0 ? "rejected" : "missing", s_snapshot.keys.size());
}

void ReportWarmup()
{
    const std::lock_guard lock(s_mutex);
    if (const auto& stats = s_warmup)
    {
        Console.WriteLn("IZPS2_NFS_TFX_WARM keys=%zu created=%u rejected=%u already=%u budget_stop=%u failed=%u elapsed_ms=%.3f",
            stats->keys, stats->created, stats->rejected, stats->already,
            unsigned(stats->budget_stop), unsigned(stats->failed), stats->elapsed_ms);
    }
}

void ReportAndSave(bool allow_write)
{
    s_armed.store(false, std::memory_order_release);
    s_owner.store(nullptr, std::memory_order_release);
    s_stop_requested.store(nullptr, std::memory_order_release);
    const std::lock_guard lock(s_mutex);
    if (!s_enabled)
        return;
    bool saved = false;
    if (allow_write && s_dirty)
    {
        const auto data = s_snapshot.Save();
        saved = GSCacheFile::WriteFramedFile(PathOnSd(), CacheKind, Stamp(), data.data(), data.size(),
            GSCacheFile::Clock::now() + std::chrono::milliseconds(750));
    }
    Console.WriteLn("IZPS2_NFS_TFX_STORE_END keys=%zu collected=%zu saved=%u", s_snapshot.keys.size(), s_collected, unsigned(saved));
}
}
