#include "embedded-spirv.h"

#include "GS/GS.h"
#include "GS/GSCompileStats.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "GS/Renderers/Vulkan/VKShaderCache.h"
#include "common/Console.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <limits>

// These ABI names are checked against the original SDK-linked archive in build-engine.py.
#define VERTEX_SYMBOL "_ZN13VKShaderCache15GetVertexShaderESt17basic_string_viewIcSt11char_traitsIcEE"
#define FRAGMENT_SYMBOL "_ZN13VKShaderCache17GetFragmentShaderESt17basic_string_viewIcSt11char_traitsIcEE"
extern "C" GSCacheFile::Stamp GetStockSpirvStamp(bool debug)
    asm("_ZN13VKShaderCache13GetSPIRVStampEb");
extern "C" VkShaderModule GetStockVertexShader(VKShaderCache*, std::string_view)
    asm("__real_" VERTEX_SYMBOL);
extern "C" VkShaderModule GetStockFragmentShader(VKShaderCache*, std::string_view)
    asm("__real_" FRAGMENT_SYMBOL);

extern "C" [[gnu::visibility("hidden")]] const u32 izps2_spirv_words[];
asm(".pushsection .rodata.izps2_spirv,\"a\",%progbits\n"
    ".p2align 2\n"
    ".global izps2_spirv_words\n"
    ".hidden izps2_spirv_words\n"
    ".type izps2_spirv_words, %object\n"
    "izps2_spirv_words:\n"
    ".incbin \"" IZPS2_SPIRV_WORDS_PATH "\"\n"
    ".size izps2_spirv_words, .-izps2_spirv_words\n"
    ".popsection\n");

namespace {
using ShaderKey = std::array<u8, 24>;
struct ShaderRecord {
    ShaderKey key;
    u32 word_offset;
    u32 word_count;
};
#include "spirv-shader-table.h"

static_assert(std::endian::native == std::endian::little);
enum class Policy { Disabled, CheckStamp, Match, Mismatch };
std::atomic<Policy> policy{Policy::Disabled};
std::atomic<u32> hits{0};
std::atomic<u32> misses{0};
std::atomic<u32> driver_rejected{0};

const ShaderRecord* FindShader(u32 stage, std::string_view source)
{
    if (source.size() > std::numeric_limits<u32>::max())
        return nullptr;
    ShaderKey key{};
    const auto digest = GSCacheFile::Hash128(source.data(), source.size());
    const u32 length = static_cast<u32>(source.size());
    std::memcpy(key.data(), digest.bytes, sizeof(digest.bytes));
    std::memcpy(key.data() + 16, &length, sizeof(length));
    std::memcpy(key.data() + 20, &stage, sizeof(stage));
    const auto end = std::end(ShaderRecords);
    const auto record = std::lower_bound(std::begin(ShaderRecords), end, key,
        [](const ShaderRecord& entry, const ShaderKey& wanted) { return entry.key < wanted; });
    return record != end && record->key == key ? record : nullptr;
}

VkShaderModule TryShader(u32 stage, std::string_view source)
{
    Policy current = policy.load(std::memory_order_acquire);
    if (current == Policy::Disabled || GSConfig.UseDebugDevice)
        return VK_NULL_HANDLE;
    if (current == Policy::CheckStamp) {
        current = GetStockSpirvStamp(false).GetDigest() == CompilerStamp ? Policy::Match : Policy::Mismatch;
        policy.store(current, std::memory_order_release);
    }
    if (current != Policy::Match)
        return VK_NULL_HANDLE;

    const ShaderRecord* record = FindShader(stage, source);
    if (!record) {
        misses.fetch_add(1, std::memory_order_relaxed);
        return VK_NULL_HANDLE;
    }
    GSDeviceVK* const device = GSDeviceVK::GetInstance();
    if (!device)
        return VK_NULL_HANDLE;
    const VkShaderModuleCreateInfo create_info{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
        size_t(record->word_count) * sizeof(u32), izps2_spirv_words + record->word_offset};
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result;
    {
        const GSCompileStats::ScopedTimer timer(GSCompileStats::ModuleCreateNs);
        result = vkCreateShaderModule(device->GetDevice(), &create_info, nullptr, &module);
    }
    if (result != VK_SUCCESS) {
        driver_rejected.fetch_add(1, std::memory_order_relaxed);
        return VK_NULL_HANDLE;
    }
    hits.fetch_add(1, std::memory_order_relaxed);
    GSCompileStats::Add(GSCompileStats::SpirvCacheHits, 1);
    return module;
}
}

void EmbeddedSpirv::Configure(bool enabled)
{
    hits.store(0, std::memory_order_relaxed);
    misses.store(0, std::memory_order_relaxed);
    driver_rejected.store(0, std::memory_order_relaxed);
    policy.store(enabled ? Policy::CheckStamp : Policy::Disabled, std::memory_order_release);
}

void EmbeddedSpirv::Report()
{
    const Policy current = policy.load(std::memory_order_acquire);
    Console.WriteLn("IZPS2_EMBEDDED_SPIRV stamp_match=%u stamp_mismatch=%u hits=%u misses=%u driver_rejected=%u",
        unsigned(current == Policy::Match), unsigned(current == Policy::Mismatch),
        hits.load(std::memory_order_relaxed), misses.load(std::memory_order_relaxed),
        driver_rejected.load(std::memory_order_relaxed));
}

extern "C" VkShaderModule GetEmbeddedVertexShader(VKShaderCache*, std::string_view)
    asm("__wrap_" VERTEX_SYMBOL);
extern "C" VkShaderModule GetEmbeddedFragmentShader(VKShaderCache*, std::string_view)
    asm("__wrap_" FRAGMENT_SYMBOL);

extern "C" VkShaderModule GetEmbeddedVertexShader(VKShaderCache* cache, std::string_view source)
{
    if (const VkShaderModule module = TryShader(0, source))
        return module;
    return GetStockVertexShader(cache, source);
}

extern "C" VkShaderModule GetEmbeddedFragmentShader(VKShaderCache* cache, std::string_view source)
{
    if (const VkShaderModule module = TryShader(1, source))
        return module;
    return GetStockFragmentShader(cache, source);
}
