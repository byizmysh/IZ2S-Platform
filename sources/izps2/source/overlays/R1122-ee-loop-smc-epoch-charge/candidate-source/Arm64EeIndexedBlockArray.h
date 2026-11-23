// EE block metadata stays in slot order. Its address-sorted index moves
// only the guest PC and slot when a block is inserted or removed.
#pragma once

#include <cstring>
#include <memory>

#include "x86/BaseblockEx.h"

class Arm64EeIndexedBlockArray
{
	struct IndexEntry
	{
		u32 startpc;
		u32 slot;
	};
	static_assert(sizeof(IndexEntry) == 8);

	std::unique_ptr<IndexEntry[]> m_order;
	std::unique_ptr<BASEBLOCKEX[]> m_metadata;
	std::unique_ptr<u32[]> m_free;
	u32 m_capacity = 0;
	u32 m_size = 0;
	u32 m_next_slot = 0;
	u32 m_free_count = 0;

	void reserve(u32 capacity)
	{
		auto order = std::make_unique_for_overwrite<IndexEntry[]>(capacity);
		auto metadata = std::make_unique_for_overwrite<BASEBLOCKEX[]>(capacity);
		auto free = std::make_unique_for_overwrite<u32[]>(capacity);
		if (m_capacity)
		{
			std::memcpy(order.get(), m_order.get(), m_size * sizeof(IndexEntry));
			std::memcpy(metadata.get(), m_metadata.get(), m_next_slot * sizeof(BASEBLOCKEX));
			std::memcpy(free.get(), m_free.get(), m_free_count * sizeof(u32));
		}

		m_order = std::move(order);
		m_metadata = std::move(metadata);
		m_free = std::move(free);
		m_capacity = capacity;
	}

public:
	explicit Arm64EeIndexedBlockArray(u32 capacity) { reserve(capacity); }

	BASEBLOCKEX* insert(u32 startpc, uptr fnptr)
	{
		if (m_next_slot == m_capacity && !m_free_count)
			reserve(m_capacity + 0x2000);

		u32 first = 0, last = m_size;
		while (first < last)
		{
			const u32 mid = first + (last - first) / 2;
			if (m_order[mid].startpc > startpc)
				last = mid;
			else
				first = mid + 1;
		}

		const u32 slot = m_free_count ? m_free[--m_free_count] : m_next_slot++;
		if (first < m_size)
			std::memmove(m_order.get() + first + 1, m_order.get() + first,
				(m_size - first) * sizeof(IndexEntry));
		m_order[first] = {startpc, slot};
		++m_size;

		auto& block = m_metadata[slot];
		block = {};
		block.startpc = startpc;
		block.fnptr = fnptr;
		return &block;
	}

	BASEBLOCKEX& operator[](int index) const { return m_metadata[m_order[index].slot]; }
	u32 startpc(int index) const { return m_order[index].startpc; }
	u32 size() const { return m_size; }

	void clear()
	{
		m_size = m_next_slot = m_free_count = 0;
	}

	// Like the original storage, removal only writes preallocated memory.
	void erase(s32 first, s32 last)
	{
		pxAssert(first >= 0 && first <= last && static_cast<u32>(last) <= m_size);
		for (s32 index = first; index < last; ++index)
			m_free[m_free_count++] = m_order[index].slot;
		if (static_cast<u32>(last) < m_size)
			std::memmove(m_order.get() + first, m_order.get() + last,
				(m_size - last) * sizeof(IndexEntry));
		m_size -= last - first;
	}
};
