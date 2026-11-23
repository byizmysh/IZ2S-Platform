// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "CDVD/ThreadedFileReader.h"

#include <cstdio>

class FlatFileReader final : public ThreadedFileReader
{
	DeclareNoncopyableObject(FlatFileReader);

#ifdef __SWITCH2__
	struct DirectSdFile;
	std::unique_ptr<DirectSdFile> m_direct_sd;
	struct DiscReadStats
	{
		u64 calls = 0, bytes = 0, ticks = 0, errors = 0;
		void Add(u64 elapsed, u64 delivered, bool success);
	};
	DiscReadStats m_direct_reads, m_storage_reads;
	bool m_disc_trial = false;
#endif
	std::FILE* m_file = nullptr;
	std::unique_ptr<u8[]> m_file_cache;
	u64 m_file_size = 0;

public:
	FlatFileReader();
	~FlatFileReader() override;

	bool Open2(std::string filename, Error* error) override;

	bool Precache2(ProgressCallback* progress, Error* error) override;

	Chunk ChunkForOffset(u64 offset) override;
	int ReadChunk(void* dst, s64 blockID) override;

	void Close2() override;

	u32 GetBlockCount() const override;
};
