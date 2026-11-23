// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/GSCompileStats.h"
#include "GS/GSShaderCompileIndicator.h"
#include "GS/GS.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "GS/Renderers/Vulkan/VKBuilders.h"
#include "GS/Renderers/Vulkan/VKShaderCache.h"
#if defined(PS2_NATIVE_GSVK_EXTERNAL) && defined(PS2_NATIVE_SPIRV_CACHE)
#include "native_spirv_store.h"
#include "native_spirv_identity.h"
#include "native_pipeline_cache_header.h"
#include <cstdlib>
#include <cstdio>
#endif

#include "BuildVersion.h"
#include "Config.h"
#include "ShaderCacheVersion.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/DynamicLibrary.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include "fmt/format.h"
#include "shaderc/shaderc.h"

#include <cstring>
#include <memory>
#include <utility>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include "common/StringUtil.h"
#elif !defined(__ANDROID__) && !defined(ARMSX2_LINK_SHADERC)
#include <dlfcn.h>
#endif

std::unique_ptr<VKShaderCache> g_vulkan_shader_cache;

static u32 s_next_bad_shader_id = 0;

/// The SPIR-V packs' one entry type: a module under its source's hash, length and stage.
static constexpr u16 SPIRV_TYPE = 1;
#if defined(PS2_NATIVE_GSVK_EXTERNAL) && defined(PS2_NATIVE_SPIRV_CACHE)
// Only the GS owner reads or writes this flag. The RAM store hands back its
// CPU-owned storage after VKShaderCache destruction and the existing GS join.
static bool s_native_spirv_attached = false;
static size_t s_native_pipeline_loaded_bytes = 0;
static u32 s_native_pipeline_load_result = 0;

#endif

namespace
{
#pragma pack(push, 4)
	struct VK_PIPELINE_CACHE_HEADER
	{
		u32 header_length;
		u32 header_version;
		u32 vendor_id;
		u32 device_id;
		u8 uuid[VK_UUID_SIZE];
	};
#pragma pack(pop)
} // namespace

static bool ValidatePipelineCacheHeader(const VK_PIPELINE_CACHE_HEADER& header)
{
	if (header.header_length < sizeof(VK_PIPELINE_CACHE_HEADER))
	{
		Console.Error("Pipeline cache failed validation: Invalid header length");
		return false;
	}

	if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
	{
		Console.Error("Pipeline cache failed validation: Invalid header version");
		return false;
	}

	if (header.vendor_id != GSDeviceVK::GetInstance()->GetDeviceProperties().vendorID)
	{
		Console.Error("Pipeline cache failed validation: Incorrect vendor ID (file: 0x%X, device: 0x%X)",
			header.vendor_id, GSDeviceVK::GetInstance()->GetDeviceProperties().vendorID);
		return false;
	}

	if (header.device_id != GSDeviceVK::GetInstance()->GetDeviceProperties().deviceID)
	{
		Console.Error("Pipeline cache failed validation: Incorrect device ID (file: 0x%X, device: 0x%X)",
			header.device_id, GSDeviceVK::GetInstance()->GetDeviceProperties().deviceID);
		return false;
	}

	if (std::memcmp(header.uuid, GSDeviceVK::GetInstance()->GetDeviceProperties().pipelineCacheUUID, VK_UUID_SIZE) != 0)
	{
		Console.Error("Pipeline cache failed validation: Incorrect UUID");
		return false;
	}

	return true;
}

#if defined(__ANDROID__) || defined(ARMSX2_LINK_SHADERC)

// shaderc is linked in - Android always, and any build that found the static
// archive (the libretro core, which cannot rely on the host having one) - so
// call the functions directly instead of going looking for a library.
namespace dyn_shaderc
{
	static bool Open();
	static void Close();

	static shaderc_compiler_t s_compiler = nullptr;

	// Direct function pointers to the statically-linked shaderc.
	static constexpr auto shaderc_compiler_initialize = ::shaderc_compiler_initialize;
	static constexpr auto shaderc_compiler_release = ::shaderc_compiler_release;
	static constexpr auto shaderc_compile_options_initialize = ::shaderc_compile_options_initialize;
	static constexpr auto shaderc_compile_options_release = ::shaderc_compile_options_release;
	static constexpr auto shaderc_compile_options_set_source_language = ::shaderc_compile_options_set_source_language;
	static constexpr auto shaderc_compile_options_set_generate_debug_info = ::shaderc_compile_options_set_generate_debug_info;
	static constexpr auto shaderc_compile_options_set_optimization_level = ::shaderc_compile_options_set_optimization_level;
	static constexpr auto shaderc_compile_options_set_target_env = ::shaderc_compile_options_set_target_env;
	static constexpr auto shaderc_compile_into_spv = ::shaderc_compile_into_spv;
	static constexpr auto shaderc_result_release = ::shaderc_result_release;
	static constexpr auto shaderc_result_get_length = ::shaderc_result_get_length;
	static constexpr auto shaderc_result_get_num_warnings = ::shaderc_result_get_num_warnings;
	static constexpr auto shaderc_result_get_bytes = ::shaderc_result_get_bytes;
	static constexpr auto shaderc_result_get_error_message = ::shaderc_result_get_error_message;
	static constexpr auto shaderc_result_get_compilation_status = ::shaderc_result_get_compilation_status;
} // namespace dyn_shaderc

bool dyn_shaderc::Open()
{
	if (s_compiler)
		return true;

	s_compiler = shaderc_compiler_initialize();
	if (!s_compiler)
	{
		ERROR_LOG("shaderc_compiler_initialize() failed");
		return false;
	}

	std::atexit(&dyn_shaderc::Close);
	return true;
}

void dyn_shaderc::Close()
{
	if (s_compiler)
	{
		shaderc_compiler_release(s_compiler);
		s_compiler = nullptr;
	}
}

#else // dlopen the system's shaderc

#define SHADERC_FUNCTIONS(X) \
	X(shaderc_compiler_initialize) \
	X(shaderc_compiler_release) \
	X(shaderc_compile_options_initialize) \
	X(shaderc_compile_options_release) \
	X(shaderc_compile_options_set_source_language) \
	X(shaderc_compile_options_set_generate_debug_info) \
	X(shaderc_compile_options_set_optimization_level) \
	X(shaderc_compile_options_set_target_env) \
	X(shaderc_compile_into_spv) \
	X(shaderc_result_release) \
	X(shaderc_result_get_length) \
	X(shaderc_result_get_num_warnings) \
	X(shaderc_result_get_bytes) \
	X(shaderc_result_get_error_message) \
	X(shaderc_result_get_compilation_status)

// TODO: NOT thread safe, yet.
namespace dyn_shaderc
{
	static bool Open();
	static void Close();

	static DynamicLibrary s_library;
	static shaderc_compiler_t s_compiler = nullptr;

#define ADD_FUNC(F) static decltype(&::F) F;
	SHADERC_FUNCTIONS(ADD_FUNC)
#undef ADD_FUNC

} // namespace dyn_shaderc

bool dyn_shaderc::Open()
{
	if (s_library.IsOpen())
		return true;

	Error error;

#ifdef _WIN32
	const std::string libname = DynamicLibrary::GetVersionedFilename("shaderc_shared");
#else
	// Use versioned, bundle post-processing adds it..
	const std::string libname = DynamicLibrary::GetVersionedFilename("shaderc_shared", 1);
	// Debian packages the library as libshaderc.so.1
	const std::string libname_fallback = DynamicLibrary::GetVersionedFilename("shaderc", 1);
#endif
	if (!s_library.Open(libname.c_str(), &error)
#ifndef _WIN32
		// libname_fallback only exists on non-Windows (see above); Windows has no
		// versioned/Debian .so fallback to try.
		&& !s_library.Open(libname_fallback.c_str(), &error)
#endif
	)
	{
		ERROR_LOG("Failed to load shaderc: {}", error.GetDescription());
		return false;
	}

#define LOAD_FUNC(F) \
	if (!s_library.GetSymbol(#F, &F)) \
	{ \
		ERROR_LOG("Failed to find function {}", #F); \
		Close(); \
		return false; \
	}

	SHADERC_FUNCTIONS(LOAD_FUNC)
#undef LOAD_FUNC

	s_compiler = shaderc_compiler_initialize();
	if (!s_compiler)
	{
		ERROR_LOG("shaderc_compiler_initialize() failed");
		Close();
		return false;
	}

	std::atexit(&dyn_shaderc::Close);
	return true;
}

void dyn_shaderc::Close()
{
	if (s_compiler)
	{
		shaderc_compiler_release(s_compiler);
		s_compiler = nullptr;
	}

#define UNLOAD_FUNC(F) F = nullptr;
	SHADERC_FUNCTIONS(UNLOAD_FUNC)
#undef UNLOAD_FUNC

	s_library.Close();
}

#undef SHADERC_FUNCTIONS
#undef SHADERC_INIT_FUNCTIONS

#endif // __ANDROID__ || ARMSX2_LINK_SHADERC

// Compilation itself is thread-safe on one compiler object; loading it is not.
static std::mutex s_shaderc_open_mutex;

static bool OpenShaderCompiler()
{
	std::unique_lock lock(s_shaderc_open_mutex);
	return dyn_shaderc::Open();
}

/// What produced the SPIR-V: part of the SPIR-V cache's stamp. The GLSL source is the key of every
/// entry, so a new build keeps SPIR-V it would compile identically; this covers the compiler itself.
static std::string GetShaderCompilerIdentity()
{
#if defined(__ANDROID__) || defined(ARMSX2_LINK_SHADERC)
	// The compiler is linked into this binary, so the binary stands for it.
	return fmt::format("linked {} {}", BuildVersion::GitHash, GSCacheFile::GetBuildId());
#else
	if (!OpenShaderCompiler())
		return "none";

	// The library that was loaded. Its path, size and modification time change with any update to it.
	std::string path;
	const void* const fn = reinterpret_cast<const void*>(dyn_shaderc::shaderc_compile_into_spv);
#ifdef _WIN32
	HMODULE module = nullptr;
	wchar_t wpath[MAX_PATH] = {};
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCWSTR>(fn), &module) &&
		GetModuleFileNameW(module, wpath, MAX_PATH) > 0)
	{
		path = StringUtil::WideStringToUTF8String(wpath);
	}
#else
	Dl_info info = {};
	if (dladdr(fn, &info) != 0 && info.dli_fname)
		path = info.dli_fname;
#endif
	FILESYSTEM_STAT_DATA sd = {};
	if (path.empty() || !FileSystem::StatFile(path.c_str(), &sd))
		return fmt::format("shared {} unknown", path);
	return fmt::format("shared {} {} {}", path, sd.Size, sd.ModificationTime);
#endif
}

static void DumpBadShader(std::string_view code, std::string_view errors)
{
	const std::string filename = Path::Combine(EmuFolders::Logs, fmt::format("pcsx2_bad_shader_{}.txt", ++s_next_bad_shader_id));
	auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "wb");
	if (fp)
	{
		if (!code.empty())
			std::fwrite(code.data(), code.size(), 1, fp.get());
		std::fputs("\n\n**** ERRORS ****\n", fp.get());
		if (!errors.empty())
			std::fwrite(errors.data(), errors.size(), 1, fp.get());
	}
}

static const char* compilation_status_to_string(shaderc_compilation_status status)
{
	switch (status)
	{
#define CASE(x) case shaderc_compilation_status_##x: return #x
		CASE(success);
		CASE(invalid_stage);
		CASE(compilation_error);
		CASE(internal_error);
		CASE(null_result_object);
		CASE(invalid_assembly);
		CASE(validation_error);
		CASE(transformation_error);
		CASE(configuration_error);
#undef CASE
	}
	return "unknown_error";
}

std::optional<VKShaderCache::SPIRVCodeVector> VKShaderCache::CompileShaderToSPV(u32 stage, std::string_view source, bool debug)
{
	std::optional<VKShaderCache::SPIRVCodeVector> ret;
	if (!OpenShaderCompiler())
		return ret;

	const GSShaderCompileIndicator::CompileTimer compile_timer;
	const GSCompileStats::ScopedTimer stats_timer(GSCompileStats::SpirvCompileNs);
	GSCompileStats::Add(GSCompileStats::SpirvCompiles, 1);

	shaderc_compile_options_t options = dyn_shaderc::shaderc_compile_options_initialize();
	pxAssertRel(options, "shaderc_compile_options_initialize() failed");

	dyn_shaderc::shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
	dyn_shaderc::shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, 0);
#ifdef SHADERC_PCSX2_CUSTOM
	dyn_shaderc::shaderc_compile_options_set_generate_debug_info(options, debug,
		debug && GSDeviceVK::GetInstance()->GetOptionalExtensions().vk_khr_shader_non_semantic_info);
#else
	if (debug)
		dyn_shaderc::shaderc_compile_options_set_generate_debug_info(options);
#endif
	dyn_shaderc::shaderc_compile_options_set_optimization_level(
		options, debug ? shaderc_optimization_level_zero : shaderc_optimization_level_performance);

	const shaderc_compilation_result_t result = dyn_shaderc::shaderc_compile_into_spv(
		dyn_shaderc::s_compiler, source.data(), source.length(), static_cast<shaderc_shader_kind>(stage), "source",
		"main", options);

	shaderc_compilation_status status = shaderc_compilation_status_null_result_object;
	if (!result || (status = dyn_shaderc::shaderc_result_get_compilation_status(result)) != shaderc_compilation_status_success)
	{
		const std::string_view errors(result ? dyn_shaderc::shaderc_result_get_error_message(result)
		                                     : "null result object");
		ERROR_LOG("Failed to compile shader to SPIR-V: {}\n{}", compilation_status_to_string(status), errors);
		DumpBadShader(source, errors);
	}
	else
	{
		const size_t num_warnings = dyn_shaderc::shaderc_result_get_num_warnings(result);
		if (num_warnings > 0)
			WARNING_LOG("Shader compiled with warnings:\n{}", dyn_shaderc::shaderc_result_get_error_message(result));

		const size_t spirv_size = dyn_shaderc::shaderc_result_get_length(result);
		const char* bytes = dyn_shaderc::shaderc_result_get_bytes(result);
		pxAssert(spirv_size > 0 && ((spirv_size % sizeof(u32)) == 0));
		ret = VKShaderCache::SPIRVCodeVector(reinterpret_cast<const u32*>(bytes),
			reinterpret_cast<const u32*>(bytes + spirv_size));
	}

	dyn_shaderc::shaderc_result_release(result);
	dyn_shaderc::shaderc_compile_options_release(options);
	return ret;
}


VKShaderCache::VKShaderCache() = default;

VKShaderCache::~VKShaderCache()
{
#if defined(PS2_NATIVE_GSVK_EXTERNAL) && defined(PS2_NATIVE_SPIRV_CACHE)
	if (s_native_spirv_attached)
	{
		// GSDeviceVK stops precompile workers before destroying us. Snapshot
		// while the original device/cache still exist; no SD I/O on the GS.
		size_t bytes = 0, saved = 0;
		VkResult result = VK_SUCCESS;
		if (m_pipeline_cache != VK_NULL_HANDLE && m_pipeline_cache_dirty.load(std::memory_order_relaxed))
		{
			result = vkGetPipelineCacheData(m_device, m_pipeline_cache, &bytes, nullptr);
			if (result == VK_SUCCESS && bytes >= 32 && bytes <= NativeSpirvStore::PIPELINE_MAX_BYTES)
			{
				void* data = std::malloc(bytes);
				if (data)
				{
					size_t actual = bytes;
					result = vkGetPipelineCacheData(m_device, m_pipeline_cache, &actual, data);
					const auto& props = GSDeviceVK::GetInstance()->GetDeviceProperties();
					if (result == VK_SUCCESS && actual <= bytes &&
						NativePipelineCacheHeader::Valid(data, actual, NativeSpirvStore::PIPELINE_MAX_BYTES,
							props.vendorID, props.deviceID, props.pipelineCacheUUID) &&
						NativeSpirvStore::PutPipeline(data, actual)) saved = actual;
					std::free(data);
				}
			}
		}
		std::fprintf(stderr, "PS2_NATIVE_PIPELINE_CACHE loadedBytes=%zu loadResult=%u queriedBytes=%zu savedBytes=%zu snapshotResult=%d cap=%zu optional=1\n",
			s_native_pipeline_loaded_bytes, s_native_pipeline_load_result, bytes, saved, int(result), NativeSpirvStore::PIPELINE_MAX_BYTES);
		NativeSpirvStore::EndGS();
		s_native_spirv_attached = false;
	}
#endif
	// The last pass writes the packs and the pipeline cache within the close deadline. It needs the
	// device, which outlives this.
	m_writer.Stop(GSCacheFile::Clock::now() + GSCacheFile::PackWriter::CLOSE_DEADLINE);
	ClosePipelineCache();
}

void VKShaderCache::Create()
{
	pxAssert(!g_vulkan_shader_cache);
	g_vulkan_shader_cache.reset(new VKShaderCache());
	g_vulkan_shader_cache->Open();

#if defined(PS2_NATIVE_GSVK_EXTERNAL) && defined(PS2_NATIVE_SPIRV_CACHE)
    // Match AddShaderHeader: version/profile AND both actual shader stages.
    // A GLSL450 fragment does not initialize the GLSL460 vertex/fragment
    // built-in tables used by real TFX programs. No Vulkan or store mutation.
    static constexpr std::string_view fragment_prime = R"PS2_PRIME(#version 460 core
#extension GL_EXT_samplerless_texture_functions : require
layout(set=0,binding=0) uniform texture2D sourceTexture;
layout(set=0,binding=1) uniform sampler sourceSampler;
layout(std140,set=0,binding=2) uniform PrimeConstants {vec4 scale;uvec4 mask;};
layout(location=0) in vec4 coordinate;
layout(location=0) out vec4 color;
void main(){
 vec2 uv=coordinate.xy;
 vec4 c=texture(sampler2D(sourceTexture,sourceSampler),uv);
 vec4 f=texelFetch(sourceTexture,ivec2(uv),0);
 uvec4 bits=uvec4(trunc(clamp(c,0.0,1.0)*255.0));
 bits=(bits & mask) | ((bits>>3u)&uvec4(31u));
 vec4 a=vec4(bits)*scale;
 for(int i=0;i<4;++i){a=trunc(mix(a,f,clamp(coordinate.z,0.0,1.0)));a=min(a+floor(c*128.0),vec4(255.0));}
 if(a.a<coordinate.w)discard;
 color=clamp(a/255.0,0.0,1.0);
}
)PS2_PRIME";
    static constexpr std::string_view vertex_prime =
        "#version 460 core\nvoid main(){gl_Position=vec4(0.0,0.0,0.0,1.0); }\n";
    const auto before=GSCompileStats::Get(GSCompileStats::SpirvCompileNs);
    const auto fragment=CompileShaderToSPV(shaderc_fragment_shader,fragment_prime,false);
    const auto vertex=CompileShaderToSPV(shaderc_vertex_shader,vertex_prime,false);
    const auto after=GSCompileStats::Get(GSCompileStats::SpirvCompileNs);
    std::fprintf(stderr,"PS2_NATIVE_SHADER_COMPILER_PRIME diagnosticOnly=1 ownershipAuthority=0 beforeGuest=1 representative=1 glsl=460 coreProfile=1 stages=2 fragment=%u vertex=%u compileNs=%llu newWorkers=0 persistentPackChanged=0\n",
        unsigned(fragment.has_value()),unsigned(vertex.has_value()),
        (unsigned long long)(after>=before?after-before:0));
#endif
}

void VKShaderCache::Destroy()
{
	g_vulkan_shader_cache.reset();
}

GSCacheFile::Stamp VKShaderCache::GetSPIRVStamp(bool debug)
{
	// No build identity and no driver: the key of every entry is the whole GLSL source text and
	// stage, and SPIR-V does not depend on the driver, so an entry is valid for as long as the
	// compiler and its options are the same. That is what keeps SPIR-V across a driver update.
	GSCacheFile::Stamp stamp;
	stamp.Add("shader_cache_version", SHADER_CACHE_VERSION);
	stamp.Add("compiler", GetShaderCompilerIdentity());
	stamp.Add("target", "vulkan1.0");
	stamp.Add("optimization", debug ? "zero" : "performance");
#ifdef SHADERC_PCSX2_CUSTOM
	// Debug info is non-semantic only when the device has the extension.
	stamp.Add("debug_info", debug ? (GSDeviceVK::GetInstance()->GetOptionalExtensions().vk_khr_shader_non_semantic_info ?
											 "non-semantic" :
											 "plain") :
									"none");
#else
	stamp.Add("debug_info", debug ? "plain" : "none");
#endif
	return stamp;
}

GSCacheFile::Stamp VKShaderCache::GetPipelineCacheStamp(bool debug)
{
	// Driver binaries: only valid for the driver that made them. pipelineCacheUUID alone is not
	// trusted, since drivers have shipped updates without changing it; the version and the driver's
	// own name and build string are in too. The build identity is in because what the blob holds
	// depends on this build's pipelines, and a new build starting from an empty blob is what stops
	// pipelines no build uses any more from piling up in it.
	const VkPhysicalDeviceProperties& props = GSDeviceVK::GetInstance()->GetDeviceProperties();
	const VkPhysicalDeviceDriverPropertiesKHR& drv = GSDeviceVK::GetInstance()->GetDeviceDriverProperties();
	GSCacheFile::Stamp stamp;
	stamp.Add("build", GSCacheFile::GetBuildId());
	stamp.Add("shader_cache_version", SHADER_CACHE_VERSION);
	stamp.Add("vendor", props.vendorID);
	stamp.Add("device", props.deviceID);
	stamp.Add("driver_version", props.driverVersion);
	stamp.Add("api_version", props.apiVersion);
	stamp.AddHex("pipeline_cache_uuid", props.pipelineCacheUUID, VK_UUID_SIZE);
	stamp.Add("driver_id", static_cast<u64>(drv.driverID));
	stamp.Add("driver_name", drv.driverName);
	stamp.Add("driver_info", drv.driverInfo);
	stamp.Add("debug", debug ? 1 : 0);
	return stamp;
}

void VKShaderCache::Open()
{
	m_device = GSDeviceVK::GetInstance()->GetDevice();
	if (GSConfig.DisableShaderCache)
	{
#if defined(PS2_NATIVE_GSVK_EXTERNAL) && defined(PS2_NATIVE_SPIRV_CACHE)
		// The generic PackWriter and its callback remain disabled. A bounded
		// native opaque cache shares the existing CPU-owned A/B generations.
		// Compute this conservative namespace once; GetShaderSPV reuses its
		// existing complete-source key and never performs SD I/O.
		// Fixed POD encoding avoids fmt/string allocation in this optional path.
		// The linked compiler and exact compile-policy body are pinned at build.
		u8 encoded[128]{};
		std::memcpy(encoded, NativeSpirvStore::COMPILER_POLICY_ID, 32);
		const bool debug = GSConfig.UseDebugDevice;
		const VkPhysicalDeviceProperties& props = GSDeviceVK::GetInstance()->GetDeviceProperties();
#ifdef SHADERC_PCSX2_CUSTOM
		const u32 custom = 1;
		const u32 non_semantic = debug && GSDeviceVK::GetInstance()->GetOptionalExtensions().vk_khr_shader_non_semantic_info;
#else
		const u32 custom = 0, non_semantic = 0;
#endif
		const u32 fields[] = {1, SHADER_CACHE_VERSION, u32(shaderc_source_language_glsl),
			u32(shaderc_target_env_vulkan), 0, u32(debug ? shaderc_optimization_level_zero : shaderc_optimization_level_performance),
			u32(debug), custom, non_semantic, props.vendorID, props.deviceID, props.driverVersion, props.apiVersion};
		static_assert(32 + sizeof(fields) + VK_UUID_SIZE <= sizeof(encoded));
		std::memcpy(encoded + 32, fields, sizeof(fields));
		std::memcpy(encoded + 32 + sizeof(fields), props.pipelineCacheUUID, VK_UUID_SIZE);
		const GSCacheFile::Digest native_stamp = GSCacheFile::Hash128(encoded, sizeof(encoded));
		s_native_spirv_attached = NativeSpirvStore::BeginGS(native_stamp.bytes);
		s_native_pipeline_loaded_bytes = 0; s_native_pipeline_load_result = 0;
		const u8* data = nullptr; size_t bytes = 0;
		if (s_native_spirv_attached && NativeSpirvStore::GetPipeline(&data, &bytes) &&
			NativePipelineCacheHeader::Valid(data, bytes, NativeSpirvStore::PIPELINE_MAX_BYTES,
				props.vendorID, props.deviceID, props.pipelineCacheUUID))
		{
			const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, bytes, data};
			VkPipelineCache cache = VK_NULL_HANDLE;
			const VkResult result = vkCreatePipelineCache(m_device, &ci, nullptr, &cache);
			s_native_pipeline_load_result = static_cast<u32>(result);
			if (result == VK_SUCCESS)
			{
				m_pipeline_cache = cache; s_native_pipeline_loaded_bytes = bytes;
				m_pipeline_cache_file_hash = 0; m_pipeline_cache_dirty = false;
			}
		}
		// Missing/corrupt/incompatible data or an optional driver rejection
		// always falls back to the original empty cache and demand compilation.
		if (!s_native_pipeline_loaded_bytes) CreateNewPipelineCache();
#else
		CreateNewPipelineCache();
#endif
		return;
	}

	const bool debug = GSConfig.UseDebugDevice;
	GSCacheFile::CleanStaleTempFiles(EmuFolders::Cache);

	// Each file's name carries its stamp's digest, so builds and drivers sharing a data root keep
	// separate files; the few most recently used others stay, older ones are removed.
	static constexpr u32 KEEP_IDENTITIES = 3;
	m_pipeline_cache_stamp = GetPipelineCacheStamp(debug);
	const std::string pipeline_stem = fmt::format("vulkan_pipelines{}_{}", debug ? "_debug" : "",
		GSCacheFile::ShortName(m_pipeline_cache_stamp.GetDigest()));
	GSCacheFile::PruneOtherIdentities(EmuFolders::Cache, "vulkan_pipelines", pipeline_stem, KEEP_IDENTITIES);
	m_pipeline_cache_filename = Path::Combine(EmuFolders::Cache, pipeline_stem + ".bin");
	if (!ReadExistingPipelineCache())
		CreateNewPipelineCache();

	m_writer.Start(EmuFolders::Cache, [this](GSCacheFile::Clock::time_point deadline, bool asked) {
		WritePipelineCache(deadline, asked);
	});

	// SPIR-V is kept per game, read whole when the game starts (SetGame); the shaders the device builds
	// before any game is known have a pack of their own.
	m_spirv_stamp = GetSPIRVStamp(debug);
	m_spirv_stem = fmt::format("vk_spirv{}_{}", debug ? "_debug" : "", GSCacheFile::ShortName(m_spirv_stamp.GetDigest()));
	m_device_spirv = OpenGamePack(std::string(), 0, "vk_spirv", m_spirv_stem, GSCacheFile::KIND_VK_SPIRV, m_spirv_stamp);
}

void VKShaderCache::SetGame(const std::string& serial, u32 crc)
{
	if (!m_writer.IsRunning())
		return;

	std::shared_ptr<GSCacheFile::Pack> pack;
	if (!serial.empty())
	{
		const std::string path =
			Path::Combine(GSCacheFile::GetPackDirectory(EmuFolders::Cache, serial, crc), m_spirv_stem + ".pack");
		{
			std::unique_lock lock(m_spirv_mutex);
			if (m_game_spirv && m_game_spirv->GetPath() == path)
				return;
		}
		pack = OpenGamePack(serial, crc, "vk_spirv", m_spirv_stem, GSCacheFile::KIND_VK_SPIRV, m_spirv_stamp);
	}

	std::shared_ptr<GSCacheFile::Pack> old;
	{
		std::unique_lock lock(m_spirv_mutex);
		old = std::exchange(m_game_spirv, std::move(pack));
	}
	ReleasePack(std::move(old));
}

std::shared_ptr<GSCacheFile::Pack> VKShaderCache::OpenGamePack(const std::string& serial, u32 crc,
	std::string_view prefix, const std::string& stem, GSCacheFile::Kind kind, const GSCacheFile::Stamp& stamp)
{
	if (!m_writer.IsRunning())
		return {};
	std::shared_ptr<GSCacheFile::Pack> pack =
		GSCacheFile::OpenPack(GSCacheFile::GetPackDirectory(EmuFolders::Cache, serial, crc), prefix, stem, kind, stamp);
	m_writer.Add(pack);
	return pack;
}

void VKShaderCache::ReleasePack(std::shared_ptr<GSCacheFile::Pack> pack)
{
	m_writer.Release(std::move(pack));
}

void VKShaderCache::RequestFlush()
{
	m_writer.Flush(false);
}

void VKShaderCache::ResetPipelineCache()
{
	// Nothing else may be using the cache but the writer, which the lock keeps out: the caller has
	// stopped the precompile workers and runs on the GS thread. Pipelines already built do not refer
	// to it.
	std::unique_lock lock(m_pipeline_cache_mutex);
	ClosePipelineCache();
	CreateNewPipelineCache();
}

VkPipelineCache VKShaderCache::GetPipelineCache(bool set_dirty /*= true*/)
{
	if (m_pipeline_cache == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	if (set_dirty)
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
	return m_pipeline_cache;
}

bool VKShaderCache::CreateNewPipelineCache()
{
	const VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr};
	VkResult res = vkCreatePipelineCache(m_device, &ci, nullptr, &m_pipeline_cache);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkCreatePipelineCache() failed: ");
		return false;
	}

	m_pipeline_cache_file_hash = 0;
	m_pipeline_cache_dirty = true;
	return true;
}

bool VKShaderCache::ReadExistingPipelineCache()
{
	std::vector<u8> data;
	const GSCacheFile::ReadResult res = GSCacheFile::ReadFramedFile(
		m_pipeline_cache_filename, GSCacheFile::KIND_VK_PIPELINES, m_pipeline_cache_stamp, &data);
	if (res != GSCacheFile::ReadResult::Ok)
	{
		if (res != GSCacheFile::ReadResult::Missing)
		{
			INFO_LOG("Vulkan: discarding pipeline cache '{}': {}", m_pipeline_cache_filename,
				GSCacheFile::ReadResultString(res));
		}
		return false;
	}

	// The stamp already covers the device, but the blob's own header is what the driver will judge,
	// so it has to agree too.
	VK_PIPELINE_CACHE_HEADER header;
	if (data.size() < sizeof(header))
		return false;
	std::memcpy(&header, data.data(), sizeof(header));
	if (!ValidatePipelineCacheHeader(header))
		return false;

	const VkPipelineCacheCreateInfo ci{
		VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, data.size(), data.data()};
	VkResult vres = vkCreatePipelineCache(m_device, &ci, nullptr, &m_pipeline_cache);
	if (vres != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(vres, "vkCreatePipelineCache() failed: ");
		return false;
	}

	m_pipeline_cache_file_hash = GSCacheFile::Hash64(data.data(), data.size());
	INFO_LOG("Vulkan: read {} bytes of pipeline cache", data.size());
	return true;
}

void VKShaderCache::WritePipelineCache(GSCacheFile::Clock::time_point deadline, bool asked)
{
	if (!m_pipeline_cache_dirty.load(std::memory_order_relaxed) || m_pipeline_cache_filename.empty())
		return;

	// The whole cache is serialised and written each time (it is opaque, so there is nothing to
	// append), so it is done at most once a minute while it keeps changing. Losing the last minute
	// costs only compiling those pipelines again at the next start.
	static constexpr std::chrono::seconds MIN_FLUSH_INTERVAL{60};
	const auto now = std::chrono::steady_clock::now();
	if (!asked && m_last_pipeline_cache_flush.time_since_epoch().count() != 0 &&
		(now - m_last_pipeline_cache_flush) < MIN_FLUSH_INTERVAL)
	{
		return;
	}

	// vkGetPipelineCacheData puts no external-synchronisation requirement on the cache, so this runs
	// while the GS thread and the precompile workers go on creating pipelines with it. The lock keeps
	// out only ResetPipelineCache and the destructor.
	std::unique_lock lock(m_pipeline_cache_mutex);
	if (m_pipeline_cache == VK_NULL_HANDLE)
		return;
	m_last_pipeline_cache_flush = now;

	const GSCompileStats::ScopedTimer stats_timer(GSCompileStats::CacheFlushNs);

	// Cleared before the data is read, so a pipeline created while this runs marks it dirty again
	// instead of being lost.
	m_pipeline_cache_dirty.store(false, std::memory_order_relaxed);

	size_t data_size;
	VkResult res = vkGetPipelineCacheData(m_device, m_pipeline_cache, &data_size, nullptr);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkGetPipelineCacheData() failed: ");
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
		return;
	}

	std::vector<u8> data(data_size);
	res = vkGetPipelineCacheData(m_device, m_pipeline_cache, &data_size, data.data());
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkGetPipelineCacheData() (2) failed: ");
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
		return;
	}

	data.resize(data_size);

	// Compared by content: a blob can change without changing size.
	const u64 hash = GSCacheFile::Hash64(data.data(), data.size());
	if (hash == m_pipeline_cache_file_hash)
		return;

	// Framed with a checksum and written to a temporary file that is renamed over the old one: a
	// half-written blob that still passes the driver's header check has rendered garbage on Adreno.
	if (!GSCacheFile::WriteFramedFile(m_pipeline_cache_filename, GSCacheFile::KIND_VK_PIPELINES,
			m_pipeline_cache_stamp, data.data(), data.size(), deadline))
	{
		Console.Warning("Vulkan: pipeline cache not written to '%s'%s", m_pipeline_cache_filename.c_str(),
			(GSCacheFile::Clock::now() >= deadline) ? " before the close deadline" : "");
		m_pipeline_cache_dirty.store(true, std::memory_order_relaxed);
		return;
	}
	DevCon.WriteLn("Vulkan: wrote %zu bytes of pipeline cache", data_size);
	m_pipeline_cache_file_hash = hash;
}

void VKShaderCache::ClosePipelineCache()
{
	if (m_pipeline_cache == VK_NULL_HANDLE)
		return;

	vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);
	m_pipeline_cache = VK_NULL_HANDLE;
}

#include "nfs-spirv-cache.inc"

std::optional<VKShaderCache::SPIRVCodeVector> VKShaderCache::GetShaderSPV(u32 type, std::string_view shader_code)
{
	// The key: a 128-bit hash of the whole source text, its length and the stage.
	u8 key[sizeof(GSCacheFile::Digest) + 2 * sizeof(u32)];
	const GSCacheFile::Digest digest = GSCacheFile::Hash128(shader_code.data(), shader_code.size());
	const u32 length = static_cast<u32>(shader_code.size());
	std::memcpy(&key[0], digest.bytes, sizeof(digest.bytes));
	std::memcpy(&key[16], &length, sizeof(length));
	std::memcpy(&key[20], &type, sizeof(type));
#if defined(PS2_NATIVE_GSVK_EXTERNAL) && defined(PS2_NATIVE_SPIRV_CACHE)
	if (s_native_spirv_attached)
	{
		const u32* words = nullptr;
		size_t count = 0;
		if (NativeSpirvStore::Get(key, &words, &count))
		{
			GSCompileStats::Add(GSCompileStats::SpirvCacheHits, 1);
			return SPIRVCodeVector(words, words + count);
		}
	}
#endif

    NfsSpirvSnapshot::Key native_key;
    std::memcpy(native_key.data(), key, native_key.size());
    if (auto spv = NfsSpirvCache::Find(native_key))
    {
        GSCompileStats::Add(GSCompileStats::SpirvCacheHits, 1);
        return spv;
    }

	std::shared_ptr<GSCacheFile::Pack> game;
	std::shared_ptr<GSCacheFile::Pack> device;
	{
		std::unique_lock lock(m_spirv_mutex);
		game = m_game_spirv;
		device = m_device_spirv;
	}

	std::vector<u8> data;
	for (const GSCacheFile::Pack* pack : {game.get(), device.get()})
	{
		if (!pack || !pack->Get(SPIRV_TYPE, key, sizeof(key), &data))
			continue;
		// The checksum already passed; a SPIR-V module is whole words starting with the magic number.
		static constexpr u32 SPIRV_MAGIC = 0x07230203;
		u32 magic = 0;
		if (data.size() >= sizeof(u32) && (data.size() % sizeof(u32)) == 0 &&
			(std::memcpy(&magic, data.data(), sizeof(magic)), magic == SPIRV_MAGIC))
		{
			GSCompileStats::Add(GSCompileStats::SpirvCacheHits, 1);
			SPIRVCodeVector spv(data.size() / sizeof(u32));
			std::memcpy(spv.data(), data.data(), data.size());
			return spv;
		}
		Console.Error("Vulkan: cached SPIR-V is not SPIR-V, recompiling");
	}

	std::optional<SPIRVCodeVector> spv = CompileShaderToSPV(type, shader_code, GSConfig.UseDebugDevice);
#if defined(PS2_NATIVE_GSVK_EXTERNAL) && defined(PS2_NATIVE_SPIRV_CACHE)
	if (s_native_spirv_attached && spv.has_value())
		NativeSpirvStore::Put(key, spv->data(), spv->size());
#endif
    if (spv)
        NfsSpirvCache::Record(native_key, *spv);
	if (GSCacheFile::Pack* pack = game ? game.get() : device.get(); spv.has_value() && pack)
		pack->Put(SPIRV_TYPE, key, sizeof(key), spv->data(), spv->size() * sizeof(SPIRVCodeType), 0, false);
	return spv;
}

VkShaderModule VKShaderCache::GetShaderModule(u32 type, std::string_view shader_code)
{
	std::optional<SPIRVCodeVector> spv = GetShaderSPV(type, shader_code);
	if (!spv.has_value())
		return VK_NULL_HANDLE;

	const VkShaderModuleCreateInfo ci{
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, spv->size() * sizeof(SPIRVCodeType), spv->data()};

	const GSCompileStats::ScopedTimer stats_timer(GSCompileStats::ModuleCreateNs);
	VkShaderModule mod;
	VkResult res = vkCreateShaderModule(m_device, &ci, nullptr, &mod);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkCreateShaderModule() failed: ");
		return VK_NULL_HANDLE;
	}

	return mod;
}

VkShaderModule VKShaderCache::GetVertexShader(std::string_view shader_code)
{
	return GetShaderModule(shaderc_glsl_vertex_shader, std::move(shader_code));
}

VkShaderModule VKShaderCache::GetFragmentShader(std::string_view shader_code)
{
	return GetShaderModule(shaderc_glsl_fragment_shader, std::move(shader_code));
}

VkShaderModule VKShaderCache::GetComputeShader(std::string_view shader_code)
{
	return GetShaderModule(shaderc_glsl_compute_shader, std::move(shader_code));
}
