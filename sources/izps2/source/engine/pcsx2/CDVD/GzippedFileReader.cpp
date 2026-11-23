// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Config.h"
#include "GzippedFileReader.h"
#include "Host.h"
#include "CDVD/GzipIndexFile.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Error.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "common/Timer.h"
#if defined(__SWITCH2__)
#include "common/SHA256.h"
#include "ps2_sb4_vm_fs.h"
#endif

#include "fmt/format.h"

#include <algorithm>

static Access* ReadIndexFromFile(const char* filename, s64 compressed_size, s32 span)
{
	auto fp = FileSystem::OpenManagedCFile(filename, "rb");
	return GzipIndexFile::Read(fp.get(), compressed_size, span);
}

static void WriteIndexToFile(Access* index, const char* filename)
{
	auto fp = FileSystem::OpenManagedCFile(filename, "wb");
	if (!fp)
		return;

	const bool written = GzipIndexFile::Write(fp.get(), *index);
	const bool closed = std::fclose(fp.release()) == 0;
	if (!written || !closed)
		ERROR_LOG("Warning: Can't write index file to disk: '{}'", filename);
	else
		INFO_LOG("Gzip quick access index file saved to disk: '{}'", filename);
}

static const char* INDEX_TEMPLATE_KEY = "$(f)";

// template:
// must contain one and only one instance of '$(f)' (without the quotes)
// if if !canEndWithKey -> must not end with $(f)
// if starts with $(f) then it expands to the full path + file name.
// if doesn't start with $(f) then it's expanded to file name only (with extension)
// if doesn't start with $(f) and ends up relative,
//   then it's relative to base (not to cwd)
// No checks are performed if the result file name can be created.
// If this proves useful, we can move it into Path:: . Right now there's no need.
static std::string ApplyTemplate(const std::string& name, const std::string& base,
	const std::string& fileTemplate, const std::string& filename,
	bool canEndWithKey, Error* error)
{
	// both sides
	std::string trimmedTemplate(StringUtil::StripWhitespace(fileTemplate));

	std::string::size_type first = trimmedTemplate.find(INDEX_TEMPLATE_KEY);
	if (first == std::string::npos // not found
		|| first != trimmedTemplate.rfind(INDEX_TEMPLATE_KEY) // more than one instance
		|| (!canEndWithKey && first == trimmedTemplate.length() - std::strlen(INDEX_TEMPLATE_KEY)))
	{
		Error::SetStringFmt(error, "Invalid {} template '{}'.\n"
								   "Template must contain exactly one '%s' and must not end with it. Aborting.",
			name, trimmedTemplate, INDEX_TEMPLATE_KEY);
		return {};
	}

	std::string fname(filename);
	if (first > 0)
		fname = Path::GetFileName(fname); // without path

	StringUtil::ReplaceAll(&trimmedTemplate, INDEX_TEMPLATE_KEY, fname);
	if (!Path::IsAbsolute(trimmedTemplate))
		trimmedTemplate = Path::Combine(base, trimmedTemplate); // ignores appRoot if tem is absolute

	return trimmedTemplate;
}

static std::string iso2indexname(const std::string& isoname, Error* error)
{
	const std::string& appRoot = EmuFolders::DataRoot;
	return ApplyTemplate("gzip index", appRoot, Host::GetBaseStringSettingValue("EmuCore", "GzipIsoIndexTemplate", "$(f).pindex.tmp"), isoname, false, error);
}

#if defined(__SWITCH2__)
static std::string GetGzipCachePath(std::FILE* file, const char* directory, s64 size)
{
	if (size <= 0 || FileSystem::FSeek64(file, 0, SEEK_SET) != 0)
		return {};
	SHA256Digest hash;
	unsigned char buffer[64 * 1024];
	s64 remaining = size;
	while (remaining > 0)
	{
		const size_t chunk = static_cast<size_t>(std::min<s64>(remaining, sizeof(buffer)));
		const size_t obtained = std::fread(buffer, 1, chunk, file);
		if (obtained != chunk)
			break;
		hash.Update(buffer, obtained);
		remaining -= obtained;
	}
	const bool complete = remaining == 0 && !std::ferror(file);
	std::clearerr(file);
	if (FileSystem::FSeek64(file, 0, SEEK_SET) != 0 || !complete)
		return {};
	return Path::Combine(directory, hash.FinalHex() + ".pindex");
}
#endif

GzippedFileReader::GzippedFileReader() = default;

GzippedFileReader::~GzippedFileReader() = default;

bool GzippedFileReader::LoadOrCreateIndex(Error* error)
{
	// Try to read index from disk
	const s64 compressed_size = FileSystem::FSize64(m_src);
	std::string indexfile;
#if defined(__SWITCH2__)
	if (const char* cache = ps2_sb4_vm_fs_gzip_cache(m_filename.c_str()))
	{
		indexfile = GetGzipCachePath(m_src, cache, compressed_size);
		if (indexfile.empty() && FileSystem::FSeek64(m_src, 0, SEEK_SET) != 0)
			return false;
	}
	else
#endif
	{
		indexfile = iso2indexname(m_filename, error);
		if (indexfile.empty())
			return false;
	}

	if (!indexfile.empty() &&
		(m_index = ReadIndexFromFile(indexfile.c_str(), compressed_size, GZFILE_SPAN_DEFAULT)) != nullptr)
	{
		INFO_LOG("Gzip quick access index read from disk: '{}'", indexfile);
		return true;
	}

	// No valid index file. Generate an index
	Console.Warning("This may take a while (but only once). Scanning compressed file to generate a quick access index...");

	Access* index = nullptr;
	int len = build_index(m_src, GZFILE_SPAN_DEFAULT, &index);
	printf("\n"); // build_index prints progress without \n's
	if (FileSystem::FSeek64(m_src, 0, SEEK_SET) != 0)
		len = Z_ERRNO;

	if (len > 0 && index && index->uncompressed_size > 0)
	{
		m_index = index;
		if (!indexfile.empty())
			WriteIndexToFile(m_index, indexfile.c_str());
	}
	else
	{
		Error::SetStringFmt(error, "ERROR ({}): Index could not be generated for file '{}'", len, m_filename);
		free_index(index);
		return false;
	}

	return true;
}

bool GzippedFileReader::Open2(std::string filename, Error* error)
{
	Close();

	m_filename = std::move(filename);
	if (!(m_src = FileSystem::OpenCFile(m_filename.c_str(), "rb", error)) || !LoadOrCreateIndex(error))
	{
		Close();
		return false;
	}

	return true;
}

void GzippedFileReader::Close2()
{
	if (m_z_state.isValid)
	{
		inflateEnd(&m_z_state.strm);
		m_z_state = {};
	}

	if (m_src)
	{
		std::fclose(m_src);
		m_src = nullptr;
	}

	if (m_index)
	{
		free_index(m_index);
		m_index = nullptr;
	}
}

ThreadedFileReader::Chunk GzippedFileReader::ChunkForOffset(u64 offset)
{
	ThreadedFileReader::Chunk chunk = {};
	if (static_cast<s64>(offset) >= m_index->uncompressed_size)
	{
		chunk.chunkID = -1;
	}
	else
	{
		chunk.chunkID = static_cast<s64>(offset) / m_index->span;
		chunk.length = m_index->span;
		chunk.offset = static_cast<u64>(chunk.chunkID) * m_index->span;
	}

	return chunk;
}

int GzippedFileReader::ReadChunk(void* dst, s64 chunkID)
{
	if (chunkID < 0)
		return -1;

	const s64 file_offset = chunkID * m_index->span;
	const u32 read_len = static_cast<u32>(std::min<s64>(m_index->uncompressed_size - file_offset, m_index->span));
	return extract(m_src, m_index, file_offset, static_cast<unsigned char*>(dst), read_len, &m_z_state);
}

u32 GzippedFileReader::GetBlockCount() const
{
	return (m_index->uncompressed_size + (m_blocksize - 1)) / m_blocksize;
}
