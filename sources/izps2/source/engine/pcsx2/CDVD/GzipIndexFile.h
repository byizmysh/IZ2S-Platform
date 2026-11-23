// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "CDVD/zlib_indexed.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace GzipIndexFile
{
inline constexpr char Magic[] = "PCSX2.index.gzip.v1|";
inline constexpr size_t MagicBytes = sizeof(Magic) - 1;

inline Access* Read(std::FILE* input, s64 compressed_size, s32 span)
{
	if (!input || compressed_size <= 0)
		return nullptr;
	const s64 size = FileSystem::FSize64(input);
	constexpr size_t header_bytes = MagicBytes + sizeof(Access);
	if (size < static_cast<s64>(header_bytes))
		return nullptr;

	char magic[MagicBytes];
	Access header{};
	if (std::fread(magic, sizeof(magic), 1, input) != 1 || std::memcmp(magic, Magic, sizeof(magic)) ||
		std::fread(&header, sizeof(header), 1, input) != 1 || header.have <= 0 || header.size < header.have ||
		header.span != span || span <= 0 || header.uncompressed_size <= 0 ||
		header.uncompressed_size > std::numeric_limits<s64>::max() - header.span ||
		header.have > 1 + (header.uncompressed_size - 1) / header.span)
		return nullptr;

	const u64 bytes = static_cast<u64>(header.have) * sizeof(Point);
	if (bytes != static_cast<u64>(size) - header_bytes || bytes > std::numeric_limits<size_t>::max())
		return nullptr;
	Point* const points = static_cast<Point*>(std::malloc(static_cast<size_t>(bytes)));
	if (!points)
		return nullptr;
	if (std::fread(points, sizeof(Point), header.have, input) != static_cast<size_t>(header.have))
	{
		std::free(points);
		return nullptr;
	}

	for (int i = 0; i < header.have; ++i)
	{
		const Point& point = points[i];
		if (point.in < 1 || point.in >= compressed_size || point.bits < 0 || point.bits > 7 ||
			point.out < 0 || point.out >= header.uncompressed_size ||
			(i == 0 ? point.out != 0 : point.out <= points[i - 1].out || point.in < points[i - 1].in))
		{
			std::free(points);
			return nullptr;
		}
	}

	Access* const index = static_cast<Access*>(std::malloc(sizeof(Access)));
	if (!index)
	{
		std::free(points);
		return nullptr;
	}
	*index = header;
	index->list = points;
	return index;
}

inline bool Write(std::FILE* output, const Access& index)
{
	Access header = index;
	header.list = nullptr;
	return std::fwrite(Magic, MagicBytes, 1, output) == 1 &&
		std::fwrite(&header, sizeof(header), 1, output) == 1 &&
		std::fwrite(index.list, sizeof(Point), index.have, output) == static_cast<size_t>(index.have) &&
		std::fflush(output) == 0;
}
} // namespace GzipIndexFile
