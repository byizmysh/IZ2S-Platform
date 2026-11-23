#include "nfs-tfx-snapshot.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>

namespace EmuFolders { const std::string Cache="mock"; }
namespace Path { std::string Combine(const std::string& base,const std::string& name) { return base+'/'+name; } }
namespace FileSystem {
std::vector<uint8_t> data;
int64_t GetPathFileSize(const char*) { return data.size(); }
}
namespace GSCacheFile {
enum class Kind : uint32_t {};
enum class ReadResult { Ok };
using Clock = std::chrono::steady_clock;
struct Stamp { template <typename T> void Add(const char*,const T&) {} };
size_t writes=0;
ReadResult ReadFramedFile(const std::string&,Kind,const Stamp&,std::vector<uint8_t>* data)
{
    *data=FileSystem::data;
    return ReadResult::Ok;
}
bool WriteFramedFile(const std::string&,Kind,const Stamp&,const uint8_t* data,size_t size,Clock::time_point)
{
    FileSystem::data.assign(data,data+size);
    ++writes;
    return true;
}
}
struct { template<typename... Args> void WriteLn(const char*,Args...) {} } Console;
#include "nfs-tfx-cache.inc"

int main()
{
    using namespace NfsTfx;
    int device=1,other=2;
    uint32_t stop_requested=0;
    const Fingerprint first{1,2,3}, changed{1,2,4};
    const Key k1{5},k2{6};
    Reset();
    Arm(&stop_requested);
    assert(!Stopping());
    __atomic_store_n(&stop_requested, 1, __ATOMIC_RELEASE);
    assert(Stopping());
    __atomic_store_n(&stop_requested, 0, __ATOMIC_RELEASE);
    assert(s_armed.exchange(false));
    assert(Select(&device,first).empty());
    Remember(&other,k2);
    Remember(&device,k1);
    Remember(&device,k1);
    Remember(&device,k2);
    Detach(&device);
    Remember(&device,Key{7});
    ReportAndSave(true);
    assert(GSCacheFile::writes==1);
    const auto stored=FileSystem::data;
    Reset();
    Arm(&stop_requested);
    assert(s_armed.exchange(false));
    assert((Select(&device,first)==std::vector<Key>{k1,k2}));
    ReportAndSave(true);
    assert(GSCacheFile::writes==1);
    Reset();
    Arm(&stop_requested);
    assert(Select(&device,changed).empty());
    Remember(&device,Key{8});
    ReportAndSave(false);
    assert(FileSystem::data==stored && GSCacheFile::writes==1);

    Snapshot snapshot;
    snapshot.fingerprint=first;
    for (size_t i=0;i<MaxKeys;++i)
        assert(snapshot.Insert(Key{static_cast<uint8_t>(i)}));
    assert(!snapshot.Insert(Key{255}));
    const auto packed=snapshot.Save();
    assert(packed.size()==MaxPayload);
    Snapshot decoded;
    assert(decoded.Load(packed));
    assert(decoded.fingerprint==first && decoded.keys==snapshot.keys);
    assert(!decoded.Load(std::span(packed).first(packed.size()-1)));
    auto duplicate=packed;
    std::copy_n(duplicate.begin()+first.size(),sizeof(Key),duplicate.begin()+first.size()+sizeof(Key));
    assert(!decoded.Load(duplicate));
    auto oversized=packed;
    oversized.push_back(0);
    assert(!decoded.Load(oversized));
    assert(decoded.keys==snapshot.keys);
    std::cout << "{\"maxKeys\":" << MaxKeys << ",\"matchingKeysRestored\":2,\"foreignIdentityKeysRestored\":0,\"ownerGatePassed\":true,\"unsafeExitWrites\":0,\"malformedCasesRejected\":3}\n";
}
