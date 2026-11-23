#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

// The saved trace supplies block hints. This cache holds only copied guest
// words and the compiler's register analysis for those words.
class EeAnalysisCache
{
public:
	struct Hint
	{
		u32 pc;
		u32 bytes;
	};

	static constexpr u32 MaxEntries = 1024;
	static constexpr u32 MaxWords = 16384;
	static constexpr u32 MaxContinuations = 8;

	// The caller supplies an immutable snapshot, or calls on the EE owner
	// while guest execution and writes are quiescent.
	template<class Analyse>
	bool Build(std::span<const Hint> hints, std::span<const u8> ram, Analyse analyse)
	{
		if (hints.empty() || hints.size() > MaxEntries)
			return false;

		u32 word_count = 0;
		for (const auto& [pc, bytes] : hints)
		{
			if (pc % 4 || !bytes || bytes % 4 || pc > ram.size() || bytes > ram.size() - pc ||
				bytes > 4096 || bytes > 4096 - (pc & 4095) || bytes / 4 > MaxWords - word_count)
				return false;
			word_count += bytes / 4;
		}

		std::unique_ptr<Entry[]> entries(new (std::nothrow) Entry[hints.size()]);
		std::unique_ptr<u32[]> words(new (std::nothrow) u32[word_count]);
		std::unique_ptr<EEINST[]> metadata(new (std::nothrow) EEINST[word_count + hints.size()]);
		if (!entries || !words || !metadata)
			return false;

		u32 offset = 0;
		for (u32 i = 0; i < hints.size(); ++i)
		{
			const auto [pc, bytes] = hints[i];
			const u32 count = bytes / 4;
			std::memcpy(words.get() + offset, ram.data() + pc, bytes);
			const u32 analysis_offset = offset + i;
			Entry& entry = entries[i];
			entry.pc = pc;
			entry.words = count;
			entry.word_offset = offset;
			entry.analysis_offset = analysis_offset;
			entry.cop2 = analyse(pc, words.get() + offset, count, metadata.get() + analysis_offset,
				entry.continuations, entry.continuation_count);
			if (entry.continuation_count > entry.continuations.size())
				return false;
			offset += count;
		}

		auto* end = entries.get() + hints.size();
		std::sort(entries.get(), end, [](const Entry& a, const Entry& b) { return a.pc < b.pc; });
		if (std::adjacent_find(entries.get(), end, [](const Entry& a, const Entry& b) { return a.pc == b.pc; }) != end)
			return false;

		m_entries = std::move(entries);
		m_words = std::move(words);
		m_metadata = std::move(metadata);
		m_count = hints.size();
		return true;
	}

	bool Read(u32 pc, u32 bytes, const u8* live, std::span<const u32> continuations,
		EEINST* dest, u32& last_code, bool& cop2) const
	{
		if (!m_count || !live)
			return false;

		const Entry* begin = m_entries.get();
		const Entry* end = begin + m_count;
		const auto* entry = std::lower_bound(begin, end, pc,
			[](const Entry& e, u32 value) { return e.pc < value; });
		if (entry == end || entry->pc != pc || entry->words * 4 != bytes ||
			entry->continuation_count != continuations.size() ||
			!std::equal(continuations.begin(), continuations.end(), entry->continuations.begin()))
			return false;

		const u32* words = m_words.get() + entry->word_offset;
		if (std::memcmp(words, live, bytes))
			return false;

		static_assert(std::is_trivially_copyable_v<EEINST>);
		std::memcpy(dest, m_metadata.get() + entry->analysis_offset, (entry->words + 1) * sizeof(EEINST));
		last_code = words[0];
		cop2 = entry->cop2;
		return true;
	}

private:
	struct Entry
	{
		u32 pc;
		u32 words;
		u32 word_offset;
		u32 analysis_offset;
		bool cop2;
		u32 continuation_count = 0;
		std::array<u32, MaxContinuations> continuations{};
	};

	std::unique_ptr<Entry[]> m_entries;
	std::unique_ptr<u32[]> m_words;
	std::unique_ptr<EEINST[]> m_metadata;
	std::size_t m_count = 0;
};
