// SPDX-License-Identifier: GPL-3.0+
#include "ZipStream.h"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <memory>
#include <new>

namespace
{
struct ZipStream
{
	std::FILE* file;
	zip_uint64_t size;
	bool writable;
	zip_error_t error;

	ZipStream(std::FILE* input, zip_uint64_t bytes, bool write) : file(input), size(bytes), writable(write) { zip_error_init(&error); }
	~ZipStream() { zip_error_fini(&error); }

	zip_int64_t fail(int code, int system_error = 0)
	{
		zip_error_set(&error, code, system_error);
		return -1;
	}
};

zip_int64_t stream_callback(void* context, void* data, zip_uint64_t length, zip_source_cmd_t command)
{
	auto* source = static_cast<ZipStream*>(context);
	constexpr auto max_io = std::min<zip_uint64_t>(ZIP_INT64_MAX, std::numeric_limits<size_t>::max());
	switch (command)
	{
		case ZIP_SOURCE_BEGIN_WRITE:
		case ZIP_SOURCE_OPEN:
			std::clearerr(source->file);
			return fseeko(source->file, 0, SEEK_SET) ? source->fail(ZIP_ER_SEEK, errno) : 0;
		case ZIP_SOURCE_CLOSE:
			return 0;
		case ZIP_SOURCE_READ:
		{
			if (source->writable)
				return 0;
			if (length > max_io)
				return source->fail(ZIP_ER_INVAL);
			const size_t bytes = std::fread(data, 1, static_cast<size_t>(length), source->file);
			return std::ferror(source->file) ? source->fail(ZIP_ER_READ, errno) : static_cast<zip_int64_t>(bytes);
		}
		case ZIP_SOURCE_WRITE:
		{
			if (length > max_io)
				return source->fail(ZIP_ER_INVAL);
			const size_t bytes = static_cast<size_t>(length);
			if (std::fwrite(data, 1, bytes, source->file) != bytes || std::ferror(source->file))
				return source->fail(ZIP_ER_WRITE, errno);
			const off_t offset = ftello(source->file);
			if (offset < 0)
				return source->fail(ZIP_ER_TELL, errno);
			source->size = std::max(source->size, static_cast<zip_uint64_t>(offset));
			return static_cast<zip_int64_t>(bytes);
		}
		case ZIP_SOURCE_COMMIT_WRITE:
			return std::fflush(source->file) || std::ferror(source->file) ? source->fail(ZIP_ER_WRITE, errno) : 0;
		case ZIP_SOURCE_ROLLBACK_WRITE:
			return 0;
		case ZIP_SOURCE_REMOVE:
			return source->fail(ZIP_ER_OPNOTSUPP);
		case ZIP_SOURCE_STAT:
		{
			if (length < sizeof(zip_stat_t))
				return source->fail(ZIP_ER_INVAL);
			auto* stat = static_cast<zip_stat_t*>(data);
			zip_stat_init(stat);
			stat->size = source->size;
			stat->valid = ZIP_STAT_SIZE;
			return sizeof(*stat);
		}
		case ZIP_SOURCE_SEEK_WRITE:
		case ZIP_SOURCE_SEEK:
		{
			const off_t offset = ftello(source->file);
			if (offset < 0)
				return source->fail(ZIP_ER_TELL, errno);
			const auto position = zip_source_seek_compute_offset(offset, source->size, data, length, &source->error);
			if (position < 0)
				return -1;
			return fseeko(source->file, position, SEEK_SET) ? source->fail(ZIP_ER_SEEK, errno) : 0;
		}
		case ZIP_SOURCE_TELL_WRITE:
		case ZIP_SOURCE_TELL:
		{
			const off_t offset = ftello(source->file);
			return offset < 0 ? source->fail(ZIP_ER_TELL, errno) : offset;
		}
		case ZIP_SOURCE_ERROR:
			return zip_error_to_data(&source->error, data, length);
		case ZIP_SOURCE_FREE:
			if (!source->writable)
				std::fclose(source->file);
			delete source;
			return 0;
		case ZIP_SOURCE_SUPPORTS:
			return source->writable ? ZIP_SOURCE_SUPPORTS_WRITABLE : ZIP_SOURCE_SUPPORTS_SEEKABLE;
		default:
			return source->fail(ZIP_ER_OPNOTSUPP);
	}
}

zip_source_t* make_source(std::FILE* file, zip_uint64_t size, bool writable, zip_error_t* error)
{
	static_assert(sizeof(off_t) >= sizeof(zip_int64_t));
	if (!file || size > ZIP_INT64_MAX)
	{
		zip_error_set(error, ZIP_ER_INVAL, 0);
		return nullptr;
	}
	std::unique_ptr<ZipStream> context(new (std::nothrow) ZipStream(file, size, writable));
	if (!context)
	{
		zip_error_set(error, ZIP_ER_MEMORY, 0);
		return nullptr;
	}
	zip_source_t* source = zip_source_function_create(stream_callback, context.get(), error);
	if (source)
		context.release();
	return source;
}
}

zip_source_t* ZipSourceFromStream(std::FILE* file, zip_uint64_t size, zip_error_t* error)
{
	return make_source(file, size, false, error);
}

zip_source_t* ZipSourceToStream(std::FILE* file, zip_error_t* error)
{
	return make_source(file, 0, true, error);
}
