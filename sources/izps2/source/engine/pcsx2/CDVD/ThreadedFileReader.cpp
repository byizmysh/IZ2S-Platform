// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ThreadedFileReader.h"
#include "Host.h"

#include "common/Error.h"
#include "common/Console.h"
#include "common/HostSys.h"
#include "common/HostVFS.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/SmallString.h"
#include "common/Threading.h"

#include <algorithm>
#include <cstring>

// Make sure buffer size is bigger than the cutoff where PCSX2 emulates a seek
// If buffers are smaller than that, we can't keep up with linear reads
static constexpr u32 MINIMUM_SIZE = 128 * 1024;

ThreadedFileReader::ThreadedFileReader() = default;

ThreadedFileReader::~ThreadedFileReader()
{
	if (m_readThread.joinable())
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			m_quit = true;
		}
		m_condition.notify_one();
		m_readThread.join();
	}
	for (auto& buffer : m_buffer)
		if (buffer.ptr)
			free(buffer.ptr);
}

size_t ThreadedFileReader::CopyBlocks(void* dst, const void* src, size_t size) const
{
	char* cdst = static_cast<char*>(dst);
	const char* csrc = static_cast<const char*>(src);
	const char* cend = csrc + size;
	if (m_internalBlockSize)
	{
		for (; csrc < cend; csrc += m_internalBlockSize, cdst += m_blocksize)
		{
			memcpy(cdst, csrc, m_blocksize);
		}
		return cdst - static_cast<char*>(dst);
	}
	else
	{
		memcpy(dst, src, size);
		return size;
	}
}

void ThreadedFileReader::Loop()
{
	Threading::SetNameOfCurrentThread("ISO Decompress");

	std::unique_lock<std::mutex> lock(m_mtx);

	while (true)
	{
		while (!m_requestSize && !m_quit)
			m_condition.wait(lock);

		if (m_quit)
			return;

		u64 requestOffset;
		u32 requestSize;

		bool ok = true;
		m_running = true;

		for (;;)
		{
			void* ptr = m_requestPtr.load(std::memory_order_acquire);
			requestOffset = m_requestOffset;
			requestSize = m_requestSize;
			lock.unlock();

			if (ptr)
				ok = Decompress(ptr, requestOffset, requestSize);

			// There's a potential for a race here when doing synchronous reads. Basically, another request can come in,
			// after we release the lock, but before we store null to indicate we're finished. So, we do a compare-exchange
			// instead, to detect when this happens, and if so, reload all the inputs and try again.
			if (!m_requestPtr.compare_exchange_strong(ptr, nullptr, std::memory_order_release))
			{
				lock.lock();
				continue;
			}

			m_condition.notify_one();
			break;
		}

		if (ok)
		{
			// Readahead
			Chunk chunk = ChunkForOffset(requestOffset + requestSize);
			if (chunk.chunkID >= 0)
			{
				int buffersFilled = 0;
				Buffer* buf = GetBlockPtr(chunk);
				// Cancel readahead if a new request comes in
				while (buf && !m_requestPtr.load(std::memory_order_acquire))
				{
					u32 bufsize = buf->size.load(std::memory_order_relaxed);
					chunk = ChunkForOffset(buf->offset + bufsize);
					if (chunk.chunkID < 0)
						break;
					if (buf->offset + bufsize != chunk.offset || chunk.length + bufsize > buf->cap)
					{
						buffersFilled++;
						if (buffersFilled >= 2)
							break;
						buf = GetBlockPtr(chunk);
					}
					else
					{
						int amt = ReadChunk(static_cast<char*>(buf->ptr) + bufsize, chunk.chunkID);
						if (amt <= 0)
							break;
						buf->size.store(bufsize + amt, std::memory_order_release);
					}
				}
			}
		}

		lock.lock();
		if (requestSize == m_requestSize && requestOffset == m_requestOffset && !m_requestPtr)
		{
			// If no one's added more work, mark this one as done
			m_requestSize = 0;
		}

		m_running = false;
		m_condition.notify_one(); // For things waiting on m_running == false
	}
}

#ifdef IZPS2_SWITCH2_DISC_CACHE_INDEX
void ThreadedFileReader::UnlinkOwnerCacheSlot(u32 slot)
{
	OwnerCacheLink& link = m_ownerCacheLinks[slot];
	if (!link.bucket)
		return;

	u16* current = &m_ownerCacheHeads[link.bucket - 1];
	while (*current != slot + 1)
		current = &m_ownerCacheLinks[*current - 1].next;
	*current = link.next;
	link = {};
}
#endif

ThreadedFileReader::Buffer* ThreadedFileReader::GetBlockPtr(const Chunk& block)
{
	const u32 slot_count = CacheSlots();
	const u32 slot_mask = slot_count - 1;
	const auto covers = [&block](const Buffer& buf) {
		const u32 size = buf.size.load(std::memory_order_relaxed);
		return size && buf.offset <= block.offset && buf.offset + size >= block.offset + block.length;
	};
#ifdef IZPS2_SWITCH2_DISC_CACHE_INDEX
	const bool owner = m_openingThread != std::thread::id{};
	u32 bucket = 0;
	if (owner)
	{
		if (block.chunkID < 0)
			return nullptr;

		// ChunkForOffset yields disjoint chunks and only the worker coalesces adjacent chunks.
		const u64 id = static_cast<u64>(block.chunkID);
		bucket = static_cast<u32>(id ^ (id >> 10) ^ (id >> 20)) & (OWNER_CACHE_BUCKETS - 1);
		for (u16 entry = m_ownerCacheHeads[bucket]; entry; entry = m_ownerCacheLinks[entry - 1].next)
		{
			const u32 slot = entry - 1;
			Buffer& buf = m_buffer[slot];
			if (buf.offset == block.offset && covers(buf))
			{
				m_nextBuffer = (slot + 1) & slot_mask;
				return &buf;
			}
		}
	}
	else
#elif defined(IZPS2_SWITCH2_DISC_CACHE_HINTS)
	OwnerChunkHint* hint = nullptr;
	if (m_openingThread != std::thread::id{} && block.chunkID >= 0)
	{
		hint = &m_ownerChunkHints[static_cast<u64>(block.chunkID) % std::size(m_ownerChunkHints)];
		if (*hint)
		{
			const u32 slot = *hint - 1;
			Buffer& buf = m_buffer[slot];
			if (buf.offset == block.offset && covers(buf))
			{
				m_nextBuffer = (slot + 1) & slot_mask;
				return &buf;
			}
		}
	}
#endif
	{
		for (u32 i = 0; i < slot_count; i++)
		{
			if (covers(m_buffer[i]))
			{
#if defined(IZPS2_SWITCH2_DISC_CACHE_HINTS) && !defined(IZPS2_SWITCH2_DISC_CACHE_INDEX)
				if (hint)
					*hint = static_cast<OwnerChunkHint>(i + 1);
#endif
				m_nextBuffer = (i + 1) & slot_mask;
				return m_buffer + i;
			}
		}
	}

	u32 slot = m_nextBuffer;
	{
		// This can be called from both the read thread threads in ReadSync
		// Calls from ReadSync are done with the lock already held to keep the read thread out
		// Therefore we should only lock on the read thread
		std::unique_lock<std::mutex> lock(m_mtx, std::defer_lock);
		if (std::this_thread::get_id() == m_readThread.get_id())
			lock.lock();
		u32 size = std::max(block.length, MINIMUM_SIZE);
		if (m_buffer[slot].cap < size)
		{
			if (void* resized = realloc(m_buffer[slot].ptr, size))
			{
				m_buffer[slot].ptr = resized;
				m_buffer[slot].cap = size;
			}
#ifdef IZPS2_SWITCH2_DISC_CACHE_4X
			else if (slot_count > THREADED_CACHE_SLOTS)
			{
				Buffer* const end = m_buffer + slot_count;
				const Buffer* const reusable =
					std::find_if(m_buffer, end, [size](const Buffer& b) { return b.cap >= size; });
				if (reusable == end)
					return nullptr;
				slot = static_cast<u32>(reusable - m_buffer);
			}
#endif
			else
				return nullptr;
		}
#ifdef IZPS2_SWITCH2_DISC_CACHE_INDEX
		if (owner)
			UnlinkOwnerCacheSlot(slot);
#endif
		m_buffer[slot].size.store(0, std::memory_order_relaxed);
	}
	Buffer& buf = m_buffer[slot];
	int size = ReadChunk(buf.ptr, block.chunkID);
	if (size > 0)
	{
		buf.offset = block.offset;
		buf.size.store(size, std::memory_order_release);
#ifdef IZPS2_SWITCH2_DISC_CACHE_INDEX
		if (owner && static_cast<u32>(size) >= block.length)
		{
			u16& head = m_ownerCacheHeads[bucket];
			m_ownerCacheLinks[slot] = {static_cast<u16>(bucket + 1), head};
			head = static_cast<u16>(slot + 1);
		}
#elif defined(IZPS2_SWITCH2_DISC_CACHE_HINTS)
		if (hint && static_cast<u32>(size) >= block.length)
			*hint = static_cast<OwnerChunkHint>(slot + 1);
#endif
		m_nextBuffer = (slot + 1) & slot_mask;
		return &buf;
	}
	return nullptr;
}

bool ThreadedFileReader::Decompress(void* target, u64 begin, u32 size)
{
	char* write = static_cast<char*>(target);
	u32 remaining = size;
	u64 off = begin;
	while (remaining)
	{
		if (m_requestCancelled.load(std::memory_order_relaxed))
			return false;

		Chunk chunk = ChunkForOffset(off);
		if (m_internalBlockSize || chunk.offset != off || chunk.length > remaining)
		{
			Buffer* buf = GetBlockPtr(chunk);
			if (!buf)
				return false;
			u32 bufoff = off - buf->offset;
			u32 bufsize = buf->size.load(std::memory_order_relaxed);
			if (bufsize <= bufoff)
				return false;
			u32 len = std::min(bufsize - bufoff, remaining);
			write += CopyBlocks(write, static_cast<char*>(buf->ptr) + bufoff, len);
			remaining -= len;
			off += len;
		}
		else
		{
			int amt = ReadChunk(write, chunk.chunkID);
			if (amt < static_cast<int>(chunk.length))
				return false;
			write += chunk.length;
			remaining -= chunk.length;
			off += chunk.length;
		}
	}
	m_amtRead += write - static_cast<char*>(target);
	return true;
}

bool ThreadedFileReader::TryCachedRead(void*& buffer, u64& offset, u32& size, const std::lock_guard<std::mutex>&)
{
	// Run through twice so that if m_buffer[1] contains the first half and m_buffer[0] contains the second half it still works
	m_amtRead = 0;
	u64 end = 0;
	bool allDone = false;
	for (u32 i = 0; i < THREADED_CACHE_SLOTS * 2; i++)
	{
		Buffer& buf = m_buffer[i % THREADED_CACHE_SLOTS];
		u32 bufsize = buf.size.load(std::memory_order_acquire);
		if (!bufsize)
			continue;
		if (buf.offset <= offset && buf.offset + bufsize > offset)
		{
			u32 off = offset - buf.offset;
			u32 cpysize = std::min(size, bufsize - off);
			size_t read = CopyBlocks(buffer, static_cast<char*>(buf.ptr) + off, cpysize);
			m_amtRead += read;
			size -= cpysize;
			offset += cpysize;
			buffer = static_cast<char*>(buffer) + read;
			if (size == 0)
				end = buf.offset + bufsize;
		}
		// Do buffers contain the current and next block?
		if (end > 0 && buf.offset == end)
			allDone = true;
	}
	return allDone;
}

bool ThreadedFileReader::Precache(ProgressCallback* progress, Error* error)
{
	CancelAndWaitUntilStopped();
	progress->SetStatusText(SmallString::from_format(TRANSLATE_FS("CDVD", "Precaching {}..."), Path::GetFileName(m_filename)).c_str());
	return Precache2(progress, error);
}

bool ThreadedFileReader::Precache2(ProgressCallback* progress, Error* error)
{
	Error::SetStringView(error, TRANSLATE_SV("CDVD","Precaching is not supported for this file format."));
	return false;
}

bool ThreadedFileReader::CheckAvailableMemoryForPrecaching(u64 required_size, Error* error)
{
	// We want to check available physical memory instead of total.
	const u64 memory_available = GetAvailablePhysicalMemory();
	// Reserve 2GB of available memory for headroom.
	constexpr u64 memory_reserve = 2147483648;
	const u64 max_precache_size = std::max(s64{0}, static_cast<s64>(memory_available - memory_reserve));

	if (required_size > max_precache_size)
	{
		Error::SetStringFmt(error,
			TRANSLATE_FS("CDVD", "Not enough memory available for precaching ({:.2f} GB required)."),
			static_cast<double>(required_size + memory_reserve) / static_cast<double>(_1gb));
		return false;
	}

	return true;
}

bool ThreadedFileReader::Open(std::string filename, Error* error)
{
	CancelAndWaitUntilStopped();
#ifdef IZPS2_SWITCH2_DISC_CACHE_INDEX
	std::fill(std::begin(m_ownerCacheHeads), std::end(m_ownerCacheHeads), 0);
	std::fill(std::begin(m_ownerCacheLinks), std::end(m_ownerCacheLinks), OwnerCacheLink{});
#elif defined(IZPS2_SWITCH2_DISC_CACHE_HINTS)
	std::memset(m_ownerChunkHints, 0, sizeof(m_ownerChunkHints));
#endif
	m_amtRead = 0;
	m_nextBuffer = 0;
	m_requestPtr.store(nullptr, std::memory_order_relaxed);
	const auto* vfs = HostVFS::GetOps();
	const bool owner_only = vfs && vfs->read_on_opening_thread && vfs->read_on_opening_thread(filename.c_str());
	m_openingThread = owner_only ? std::this_thread::get_id() : std::thread::id{};
	if (!Open2(std::move(filename), error))
		return false;
	if constexpr (Switch2HostBufferScale > 1)
		Console.WriteLn("IZPS2_DISC_CACHE slots=%u", CacheSlots());
	if (!owner_only && !m_readThread.joinable())
		m_readThread = std::thread(&ThreadedFileReader::Loop, this);
	return true;
}

int ThreadedFileReader::ReadOnOpeningThread(void* ptr, u64 offset, u32 size)
{
	if (std::this_thread::get_id() != m_openingThread || (size && !ptr))
		return -1;
	m_requestCancelled.store(false, std::memory_order_relaxed);
	m_amtRead = 0;
	return !size || Decompress(ptr, offset, size) ? m_amtRead : -1;
}

int ThreadedFileReader::ReadSync(void* pBuffer, u32 sector, u32 count)
{
	u32 blocksize = InternalBlockSize();
	u64 offset = (u64)sector * (u64)blocksize + m_dataoffset;
	u32 size = count * blocksize;
	if (m_openingThread != std::thread::id{})
		return ReadOnOpeningThread(pBuffer, offset, size);
	{
		std::lock_guard<std::mutex> l(m_mtx);
		if (TryCachedRead(pBuffer, offset, size, l))
			return m_amtRead;

		if (size > 0 && !m_running)
		{
			// Don't wait for read thread to start back up
			if (Decompress(pBuffer, offset, size))
			{
				offset += size;
				size = 0;
			}
		}

		if (size == 0)
		{
			// For readahead
			m_requestOffset = offset - 1;
			m_requestSize = 1;
			m_requestPtr.store(nullptr, std::memory_order_relaxed);
		}
		else
		{
			m_requestOffset = offset;
			m_requestSize = size;
			m_requestPtr.store(pBuffer, std::memory_order_relaxed);
		}
		m_requestCancelled.store(false, std::memory_order_relaxed);
	}
	m_condition.notify_one();
	if (size == 0)
		return m_amtRead;
	return FinishRead();
}

void ThreadedFileReader::CancelAndWaitUntilStopped(void)
{
	m_requestCancelled.store(true, std::memory_order_relaxed);
	std::unique_lock<std::mutex> lock(m_mtx);

	// Prevent the last request being picked up, if there was one.
	// m_requestCancelled just stops the current decompress.
	m_requestSize = 0;

	while (m_running)
		m_condition.wait(lock);
}

void ThreadedFileReader::BeginRead(void* pBuffer, u32 sector, u32 count)
{
	s32 blocksize = InternalBlockSize();
	u64 offset = (u64)sector * (u64)blocksize + m_dataoffset;
	u32 size = count * blocksize;
	if (m_openingThread != std::thread::id{})
	{
		if (std::this_thread::get_id() == m_openingThread)
			m_amtRead = ReadOnOpeningThread(pBuffer, offset, size);
		return;
	}
	{
		std::lock_guard<std::mutex> l(m_mtx);
		if (TryCachedRead(pBuffer, offset, size, l))
			return;
		if (size == 0)
		{
			// For readahead
			m_requestOffset = offset - 1;
			m_requestSize = 1;
			m_requestPtr.store(nullptr, std::memory_order_relaxed);
		}
		else
		{
			m_requestOffset = offset;
			m_requestSize = size;
			m_requestPtr.store(pBuffer, std::memory_order_relaxed);
		}
		m_requestCancelled.store(false, std::memory_order_relaxed);
	}
	m_condition.notify_one();
}

int ThreadedFileReader::FinishRead(void)
{
	if (m_openingThread != std::thread::id{})
		return std::this_thread::get_id() == m_openingThread ? m_amtRead : -1;
	if (m_requestPtr.load(std::memory_order_acquire) == nullptr)
		return m_amtRead;
	std::unique_lock<std::mutex> lock(m_mtx);
	while (m_requestPtr.load(std::memory_order_acquire))
		m_condition.wait(lock);
	return m_amtRead;
}

void ThreadedFileReader::CancelRead(void)
{
	if (m_openingThread != std::thread::id{})
		return;
	if (m_requestPtr.load(std::memory_order_acquire) == nullptr)
		return;
	m_requestCancelled.store(true, std::memory_order_release);
	std::unique_lock<std::mutex> lock(m_mtx);
	while (m_requestPtr.load(std::memory_order_relaxed))
		m_condition.wait(lock);
}

void ThreadedFileReader::Close(void)
{
	CancelAndWaitUntilStopped();
	for (auto& buf : m_buffer)
		buf.size.store(0, std::memory_order_relaxed);
	Close2();
}

void ThreadedFileReader::SetBlockSize(u32 bytes)
{
	m_blocksize = bytes;
}

void ThreadedFileReader::SetDataOffset(u32 bytes)
{
	m_dataoffset = bytes;
}
