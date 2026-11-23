// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "FlatFileReader.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Error.h"

#include <cerrno>
#include <cstring>

#if defined(__SWITCH2__) && !defined(IZPS2_HOSTED_WORKER)
#include "common/Path.h"
#include "common/StringUtil.h"
#include "ps2_sb4_layout.h"

// The core and libounce expose different 128-bit types under these names.
#define u128 LibounceU128
#define s128 LibounceS128
extern "C" {
#include <switch/arm/counter.h>
#include <switch/runtime/devices/fs_dev.h>
#include <switch/services/fs.h>
}
#undef s128
#undef u128
#include <array>
#include <new>

struct FlatFileReader::DirectSdFile
{
	FsFile file{};
	bool opened = false;

	~DirectSdFile()
	{
		if (opened)
			fsFileClose(&file);
	}

	bool Read(u64 offset, void* buffer, size_t bytes)
	{
		u64 received = 0;
		return R_SUCCEEDED(fsFileRead(&file, static_cast<s64>(offset), buffer, bytes, 0, &received)) &&
			received == bytes;
	}

	static bool Eligible(std::string_view filename)
	{
		constexpr std::string_view games = PS2_SB4_SDK_ROOT "/games/";
		const std::string_view name = filename.starts_with(games) ? filename.substr(games.size()) : std::string_view{};
		return !name.empty() && name.find_first_of("/\\:") == name.npos &&
			StringUtil::compareNoCase(Path::GetExtension(name), "iso");
	}

	static std::unique_ptr<DirectSdFile> Open(std::string_view filename, std::FILE* reference, s64 size)
	{
		// Borrow the mounted filesystem and own only this read-only file.
		FsFileSystem* sd = fsdevGetDeviceFileSystem("sdmc");
		if (!sd)
			return {};
		std::unique_ptr<DirectSdFile> direct(new (std::nothrow) DirectSdFile);
		if (!direct)
			return {};
		const char* native_path = filename.data() + std::string_view("/sd").size();
		if (R_FAILED(fsFsOpenFile(sd, native_path, FsOpenMode_Read, &direct->file)))
			return {};
		direct->opened = true;
		s64 native_size = 0;
		if (R_FAILED(fsFileGetSize(&direct->file, &native_size)) || native_size != size)
			return {};

		// Verify that the launcher and mounted SD resolve to the same ISO
		// geometry and volume descriptor before bypassing the storage route.
		constexpr s64 descriptor = 16 * 2048;
		std::array<u8, 2048> expected, actual;
		if (size < descriptor + static_cast<s64>(expected.size()) ||
			!direct->Read(descriptor, actual.data(), actual.size()))
			return {};
		const s64 position = FileSystem::FTell64(reference);
		if (position < 0 || FileSystem::FSeek64(reference, descriptor, SEEK_SET) != 0)
			return {};
		const size_t read = std::fread(expected.data(), 1, expected.size(), reference);
		const bool restored = FileSystem::FSeek64(reference, position, SEEK_SET) == 0;
		std::clearerr(reference);
		if (!restored || read != expected.size() || actual != expected)
			return {};
		return direct;
	}
};

void FlatFileReader::DiscReadStats::Add(u64 elapsed, u64 delivered, bool success)
{
	++calls;
	ticks += elapsed;
	bytes += delivered;
	errors += !success;
}
#elif defined(__SWITCH2__)
struct FlatFileReader::DirectSdFile {};
#endif

static constexpr size_t CHUNK_SIZE = 128 * 1024;

FlatFileReader::FlatFileReader() = default;

FlatFileReader::~FlatFileReader()
{
	pxAssert(!m_file);
}

bool FlatFileReader::Open2(std::string filename, Error* error)
{
	m_filename = std::move(filename);
	if (!(m_file = FileSystem::OpenCFile(m_filename.c_str(), "rb", error)))
		return false;

	const s64 filesize = FileSystem::FSize64(m_file);
	if (filesize <= 0)
	{
		Error::SetStringView(error, "Failed to determine file size.");
		Close2();
		return false;
	}

	m_file_size = static_cast<u64>(filesize);
#if defined(__SWITCH2__) && !defined(IZPS2_HOSTED_WORKER)
	m_direct_reads = {};
	m_storage_reads = {};
	const bool eligible = DirectSdFile::Eligible(m_filename);
	const bool direct_requested = eligible && FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/disc-libounce.flag");
	m_disc_trial = direct_requested ||
		(eligible && FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/disc-libounce-control.flag"));
	if (direct_requested)
		m_direct_sd = DirectSdFile::Open(m_filename, m_file, filesize);
	if (m_disc_trial)
		Console.WriteLn("IZPS2_DISC_READ mode=%s requested=%u",
			m_direct_sd ? "libounce" : "storage", unsigned(direct_requested));
#endif
	return true;
}

bool FlatFileReader::Precache2(ProgressCallback* progress, Error* error)
{
	if (!m_file || !CheckAvailableMemoryForPrecaching(m_file_size, error))
		return false;

	m_file_cache = std::make_unique_for_overwrite<u8[]>(m_file_size);
	if (FileSystem::FSeek64(m_file, 0, SEEK_SET) != 0 ||
		FileSystem::ReadFileWithProgress(
			m_file, m_file_cache.get(), m_file_size, progress, error) != m_file_size)
	{
		m_file_cache.reset();
		return false;
	}

#if defined(__SWITCH2__) && !defined(IZPS2_HOSTED_WORKER)
	m_direct_sd.reset();
#endif
	std::fclose(m_file);
	m_file = nullptr;
	return true;
}

ThreadedFileReader::Chunk FlatFileReader::ChunkForOffset(u64 offset)
{
	ThreadedFileReader::Chunk chunk = {};
	if (offset >= m_file_size)
	{
		chunk.chunkID = -1;
	}
	else
	{
		chunk.chunkID = offset / CHUNK_SIZE;
		chunk.length = static_cast<u32>(std::min<u64>(m_file_size - offset, CHUNK_SIZE));
		chunk.offset = static_cast<u64>(chunk.chunkID) * CHUNK_SIZE;
	}

	return chunk;
}

int FlatFileReader::ReadChunk(void* dst, s64 blockID)
{
	if (blockID < 0)
		return -1;

	const u64 file_offset = static_cast<u64>(blockID) * CHUNK_SIZE;
	if (m_file_cache)
	{
		if (file_offset >= m_file_size)
			return -1;

		const u64 read_size = std::min<u64>(m_file_size - file_offset, CHUNK_SIZE);
		std::memcpy(dst, &m_file_cache[file_offset], read_size);
		return static_cast<int>(read_size);
	}

	const u32 read_size = static_cast<u32>(std::min<u64>(m_file_size - file_offset, CHUNK_SIZE));
#if defined(__SWITCH2__) && !defined(IZPS2_HOSTED_WORKER)
	if (m_direct_sd)
	{
		const u64 start = armGetSystemTick();
		const bool ok = m_direct_sd->Read(file_offset, dst, read_size);
		m_direct_reads.Add(armGetSystemTick() - start, ok ? read_size : 0, ok);
		if (ok)
			return static_cast<int>(read_size);
		m_direct_sd.reset();
		Console.Warning("IZPS2_DISC_READ fallback=storage offset=%llu",
			static_cast<unsigned long long>(file_offset));
	}
#endif
#if defined(__SWITCH2__) && !defined(IZPS2_HOSTED_WORKER)
	const u64 start = m_disc_trial ? armGetSystemTick() : 0;
#endif
	const bool seek = FileSystem::FSeek64(m_file, file_offset, SEEK_SET) == 0;
	const bool read = seek && std::fread(dst, read_size, 1, m_file) == 1;
#if defined(__SWITCH2__) && !defined(IZPS2_HOSTED_WORKER)
	if (m_disc_trial)
		m_storage_reads.Add(armGetSystemTick() - start, read ? read_size : 0, read);
#endif
	return read ? static_cast<int>(read_size) : (seek ? 0 : -1);
}

void FlatFileReader::Close2()
{
#if defined(__SWITCH2__) && !defined(IZPS2_HOSTED_WORKER)
	m_direct_sd.reset();
	if (m_disc_trial)
	{
		Console.WriteLn("IZPS2_DISC_READ_TOTAL tick_hz=%llu direct_calls=%llu direct_bytes=%llu direct_ticks=%llu "
			"direct_errors=%llu storage_calls=%llu storage_bytes=%llu storage_ticks=%llu storage_errors=%llu",
			static_cast<unsigned long long>(armGetSystemTickFreq()),
			static_cast<unsigned long long>(m_direct_reads.calls), static_cast<unsigned long long>(m_direct_reads.bytes),
			static_cast<unsigned long long>(m_direct_reads.ticks), static_cast<unsigned long long>(m_direct_reads.errors),
			static_cast<unsigned long long>(m_storage_reads.calls), static_cast<unsigned long long>(m_storage_reads.bytes),
			static_cast<unsigned long long>(m_storage_reads.ticks), static_cast<unsigned long long>(m_storage_reads.errors));
		m_disc_trial = false;
	}
#endif
	if (!m_file)
		return;

	std::fclose(m_file);
	m_file = nullptr;
	m_file_size = 0;
}

u32 FlatFileReader::GetBlockCount() const
{
	return static_cast<u32>(m_file_size / m_blocksize);
}
