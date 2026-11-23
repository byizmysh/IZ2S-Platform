// EE metadata is stored in stable slots. The sorted PC index is split into
// leaves so an insertion cannot move the tail of the full guest address space.
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "x86/BaseblockEx.h"

class Arm64EeIndexedBlockArray
{
	struct IndexEntry
	{
		u32 startpc;
		u32 slot;
	};
	static_assert(sizeof(IndexEntry) == 8);
	static constexpr u32 EntriesPerLeaf = 256;

	struct Leaf
	{
		std::array<IndexEntry, EntriesPerLeaf> entries;
		u32 size;
		Leaf* next_free;
	};

	struct LeafRef
	{
		Leaf* leaf;
		u32 base;
	};

	std::vector<std::unique_ptr<Leaf>> m_leaves;
	std::vector<LeafRef> m_order;
	Leaf* m_free_leaves = nullptr;
	std::unique_ptr<BASEBLOCKEX[]> m_metadata;
	std::unique_ptr<u32[]> m_free;
	u32 m_capacity = 0;
	u32 m_size = 0;
	u32 m_next_slot = 0;
	u32 m_free_count = 0;

	void reserve(u32 capacity)
	{
		auto metadata = std::make_unique_for_overwrite<BASEBLOCKEX[]>(capacity);
		auto free = std::make_unique_for_overwrite<u32[]>(capacity);
		if (m_capacity)
		{
			std::memcpy(metadata.get(), m_metadata.get(), m_next_slot * sizeof(BASEBLOCKEX));
			std::memcpy(free.get(), m_free.get(), m_free_count * sizeof(u32));
		}
		m_metadata = std::move(metadata);
		m_free = std::move(free);
		m_capacity = capacity;
	}

	Leaf* acquireLeaf()
	{
		Leaf* leaf = m_free_leaves;
		if (leaf)
			m_free_leaves = leaf->next_free;
		else
		{
			m_leaves.push_back(std::make_unique_for_overwrite<Leaf>());
			leaf = m_leaves.back().get();
		}
		leaf->size = 0;
		return leaf;
	}

	void releaseLeaf(Leaf* leaf)
	{
		leaf->next_free = m_free_leaves;
		m_free_leaves = leaf;
	}

	size_t leafForPc(u32 pc) const
	{
		const auto it = std::upper_bound(m_order.begin(), m_order.end(), pc,
			[](u32 value, const LeafRef& ref) { return value < ref.leaf->entries.front().startpc; });
		return it == m_order.begin() ? 0 : static_cast<size_t>(it - m_order.begin() - 1);
	}

	size_t leafForIndex(u32 index) const
	{
		const auto it = std::upper_bound(m_order.begin(), m_order.end(), index,
			[](u32 value, const LeafRef& ref) { return value < ref.base; });
		return static_cast<size_t>(it - m_order.begin() - 1);
	}

	static IndexEntry* upperBound(Leaf& leaf, u32 pc)
	{
		return std::upper_bound(leaf.entries.data(), leaf.entries.data() + leaf.size, pc,
			[](u32 value, const IndexEntry& entry) { return value < entry.startpc; });
	}

	void refreshBases(size_t from)
	{
		u32 base = from ? m_order[from - 1].base + m_order[from - 1].leaf->size : 0;
		for (; from < m_order.size(); ++from)
		{
			auto& ref = m_order[from];
			ref.base = base;
			base += ref.leaf->size;
		}
	}

public:
	struct Lookup
	{
		int index;
		BASEBLOCKEX* block;
	};

	explicit Arm64EeIndexedBlockArray(u32 capacity) { reserve(capacity); }

	BASEBLOCKEX* insert(u32 startpc, uptr fnptr)
	{
		if (m_next_slot == m_capacity && !m_free_count)
			reserve(m_capacity + 0x2000);
		if (m_order.empty())
			m_order.push_back({acquireLeaf(), 0});

		size_t index = m_size ? leafForPc(startpc) : 0;
		Leaf* leaf = m_order[index].leaf;
		u32 position = static_cast<u32>(upperBound(*leaf, startpc) - leaf->entries.data());
		if (leaf->size == EntriesPerLeaf)
		{
			constexpr u32 Half = EntriesPerLeaf / 2;
			Leaf* upper = acquireLeaf();
			std::memcpy(upper->entries.data(), leaf->entries.data() + Half, Half * sizeof(IndexEntry));
			upper->size = Half;
			m_order.insert(m_order.begin() + index + 1, {upper, m_order[index].base + Half});
			leaf->size = Half;
			if (position >= Half)
			{
				++index;
				leaf = upper;
				position -= Half;
			}
		}

		const u32 slot = m_free_count ? m_free[--m_free_count] : m_next_slot++;
		IndexEntry* target = leaf->entries.data() + position;
		if (position < leaf->size)
			std::memmove(target + 1, target, (leaf->size - position) * sizeof(IndexEntry));
		*target = {startpc, slot};
		++leaf->size;
		++m_size;
		refreshBases(index + 1);

		auto& block = m_metadata[slot];
		block = {};
		block.startpc = startpc;
		block.fnptr = fnptr;
		return &block;
	}

	Lookup last(u32 pc) const
	{
		if (m_order.empty())
			return {-1, nullptr};
		const auto& ref = m_order[leafForPc(pc)];
		const auto* begin = ref.leaf->entries.data();
		const auto* next = upperBound(*ref.leaf, pc);
		if (next == begin)
			return {-1, nullptr};
		return {static_cast<int>(ref.base + (next - begin) - 1), &m_metadata[(next - 1)->slot]};
	}

	BASEBLOCKEX& operator[](int index) const
	{
		const auto& ref = m_order[leafForIndex(index)];
		return m_metadata[ref.leaf->entries[index - ref.base].slot];
	}

	u32 size() const { return m_size; }

	void clear()
	{
		for (const auto& ref : m_order)
			releaseLeaf(ref.leaf);
		m_order.clear();
		m_size = m_next_slot = m_free_count = 0;
	}

	// Invalidation can happen from the fastmem fault handler. Both the
	// metadata free list and the leaf directory are already allocated.
	void erase(s32 first, s32 last)
	{
		pxAssert(first >= 0 && first <= last && static_cast<u32>(last) <= m_size);
		if (first == last)
			return;
		const size_t first_leaf = leafForIndex(first);
		const size_t last_leaf = leafForIndex(last - 1);
		size_t kept = first_leaf;
		for (size_t i = first_leaf; i <= last_leaf; ++i)
		{
			const auto ref = m_order[i];
			Leaf* leaf = ref.leaf;
			const u32 from = std::max<s32>(0, first - static_cast<s32>(ref.base));
			const u32 to = std::min<u32>(leaf->size, last - ref.base);
			for (u32 pos = from; pos < to; ++pos)
				m_free[m_free_count++] = leaf->entries[pos].slot;
			if (to < leaf->size)
				std::memmove(leaf->entries.data() + from, leaf->entries.data() + to,
					(leaf->size - to) * sizeof(IndexEntry));
			leaf->size -= to - from;
			if (leaf->size)
				m_order[kept++] = ref;
			else
				releaseLeaf(leaf);
		}
		m_order.erase(m_order.begin() + kept, m_order.begin() + last_leaf + 1);
		m_size -= last - first;
		refreshBases(first_leaf);
	}
};
