// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "zip.h"

#include "Console.h"

#ifdef __SWITCH2__
#include <cerrno>
#include "HostVFS.h"
#include "ZipStream.h"

static inline zip_source_t* zip_source_scoped_or_file(const char* filename, int flags, zip_error_t* error)
{
	const auto* vfs = HostVFS::GetOps();
	if (!(flags & ZIP_RDONLY) || !vfs || !vfs->read_on_opening_thread || !vfs->read_on_opening_thread(filename))
		return zip_source_file_create(filename, 0, 0, error);

	std::unique_ptr<std::FILE, decltype(&std::fclose)> stream(HostVFS::OpenAsCFile(filename, "rb"), std::fclose);
	if (!stream)
	{
		zip_error_set(error, ZIP_ER_OPEN, errno);
		return nullptr;
	}
	s64 size;
	if (!HostVFS::SizeOfCFile(stream.get(), &size) || size < 0)
	{
		zip_error_set(error, ZIP_ER_READ, 0);
		return nullptr;
	}
	zip_source_t* source = ZipSourceFromStream(stream.get(), static_cast<zip_uint64_t>(size), error);
	if (source)
		stream.release();
	return source;
}
#endif

static inline std::unique_ptr<zip_t, void (*)(zip_t*)> zip_open_managed(const char* filename, int flags, zip_error_t* ze)
{
#ifdef __SWITCH2__
	zip_source_t* zs = zip_source_scoped_or_file(filename, flags, ze);
#else
	zip_source_t* zs = zip_source_file_create(filename, 0, 0, ze);
#endif
	zip_t* zip = nullptr;
	if (zs && !(zip = zip_open_from_source(zs, flags, ze)))
	{
		// have to clean up source
		zip_source_free(zs);
	}

	return std::unique_ptr<zip_t, void (*)(zip_t*)>(zip, [](zip_t* zf) {
		if (!zf)
			return;

		int err = zip_close(zf);
		if (err != 0)
		{
			Console.Error("Failed to close zip file: %d", err);
			zip_discard(zf);
		}
	});
}

static inline std::unique_ptr<zip_t, void (*)(zip_t*)> zip_open_buffer_managed(const void* buffer, size_t size, int flags, int freep, zip_error_t* ze)
{
	zip_source_t* zs = zip_source_buffer_create(buffer, size, freep, ze);
	zip_t* zip = nullptr;
	if (zs && !(zip = zip_open_from_source(zs, flags, ze)))
	{
		// have to clean up source
		zip_source_free(zs);
	}

	return std::unique_ptr<zip_t, void (*)(zip_t*)>(zip, [](zip_t* zf) {
		if (!zf)
			return;

		int err = zip_close(zf);
		if (err != 0)
		{
			Console.Error("Failed to close zip file: %d", err);
			zip_discard(zf);
		}
	});
}

static inline std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> zip_fopen_managed(zip_t* zip, const char* filename, zip_flags_t flags)
{
	return std::unique_ptr<zip_file_t, int (*)(zip_file_t*)>(zip_fopen(zip, filename, flags), zip_fclose);
}

static inline std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> zip_fopen_index_managed(zip_t* zip, zip_uint64_t index, zip_flags_t flags)
{
	return std::unique_ptr<zip_file_t, int (*)(zip_file_t*)>(zip_fopen_index(zip, index, flags), zip_fclose);
}

template<typename T>
static inline std::optional<T> ReadFileInZipToContainer(zip_t* zip, const char* name)
{
	std::optional<T> ret;
	const zip_int64_t file_index = zip_name_locate(zip, name, ZIP_FL_NOCASE);
	if (file_index >= 0)
	{
		zip_stat_t zst;
		if (zip_stat_index(zip, file_index, ZIP_FL_NOCASE, &zst) == 0)
		{
			zip_file_t* zf = zip_fopen_index(zip, file_index, ZIP_FL_NOCASE);
			if (zf)
			{
				ret = T();
				ret->resize(static_cast<size_t>(zst.size));
				if (zip_fread(zf, ret->data(), ret->size()) != static_cast<zip_int64_t>(ret->size()))
				{
					ret.reset();
				}
			}
		}
	}

	return ret;
}


template <typename T>
static inline std::optional<T> ReadFileInZipToContainer(zip_file_t* file, u32 chunk_size = 4096)
{
	std::optional<T> ret = T();
	for (;;)
	{
		const size_t pos = ret->size();
		ret->resize(pos + chunk_size);
		const s64 read = zip_fread(file, ret->data() + pos, chunk_size);
		if (read < 0)
		{
			// read error
			ret.reset();
			break;
		}

		// if less than chunk size, we're EOF
		if (read != static_cast<s64>(chunk_size))
		{
			ret->resize(pos + static_cast<size_t>(read));
			break;
		}
	}

	return ret;
}


static inline std::optional<std::string> ReadFileInZipToString(zip_t* zip, const char* name)
{
	return ReadFileInZipToContainer<std::string>(zip, name);
}

static inline std::optional<std::string> ReadFileInZipToString(zip_file_t* file, u32 chunk_size = 4096)
{
	return ReadFileInZipToContainer<std::string>(file, chunk_size);
}

static inline std::optional<std::vector<u8>> ReadBinaryFileInZip(zip_t* zip, const char* name)
{
	return ReadFileInZipToContainer<std::vector<u8>>(zip, name);
}

static inline std::optional<std::vector<u8>> ReadBinaryFileInZip(zip_file_t* file, u32 chunk_size = 4096)
{
	return ReadFileInZipToContainer<std::vector<u8>>(file, chunk_size);
}
