#pragma once

#include "arm64/EeSoftmemPages.h"
#include "EeSoftmemWriteTag.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <memory>
#include <new>

class EeCodeWriteIndex
{
public:
    static constexpr uint32_t PageSize = 4096;
    static constexpr uint32_t VirtualPages = 1u << 20;
    static constexpr uint32_t None = UINT32_MAX;

    EeCodeWriteIndex(uintptr_t main, uint32_t size, std::span<uintptr_t, 2u * VirtualPages> entries)
        : main_(main), size_(size), entries_(entries),
          pages_(size / PageSize), next_(new (std::nothrow) uint32_t[VirtualPages]),
          head_(new (std::nothrow) uint32_t[pages_]), code_(new (std::nothrow) uint32_t[pages_]{})
    {
        if (!ready())
            return;
        std::fill_n(head_.get(), pages_, None);
        // Admit the already mapped RAM only after the allocations succeed.
        for (uint32_t i = 0; i < VirtualPages; ++i)
        {
            const uintptr_t original = entries_[i];
            entries_[i] = EeSoftmemIndirectPage;
            entries_[VirtualPages + i] = EeSoftmemIndirectPage;
            setVirtualPage(i * PageSize, original);
        }
    }

    bool ready() const { return next_ && head_ && code_; }

    void setVirtualPage(uint32_t address, uintptr_t taggedBias)
    {
        const uint32_t index = address / PageSize;
        if (entries_[index] == taggedBias)
            return;
        const uint32_t previous = physicalPage(entries_[index], address);
        if (previous != None)
        {
            // Alias lists are only traversed when the guest remaps a page.
            auto* link = &head_[previous];
            while (*link != index)
                link = &next_[*link];
            *link = next_[index];
        }

        entries_[index] = taggedBias;
        const uint32_t physical = physicalPage(taggedBias, address);
        if (physical != None)
        {
            next_[index] = head_[physical];
            head_[physical] = index;
        }
        refreshAlias(index, physical);
    }

    void markCodeSpan(uint32_t physical, uint32_t begin, uint32_t end)
    {
        setCodeMask(physical, code_[physical] | EeWriteChunkMask(begin, end));
    }

    void resetCodePages()
    {
        for (uint32_t page = 0; page < pages_; ++page)
            setCodeMask(page, 0);
    }

    // The caller passes a canonical Main RAM span and invalidates its
    // affected code pages before writing any bytes.
    template<typename Invalidate>
    void beforeCanonicalWrite(uint32_t offset, uint32_t bytes, Invalidate&& invalidate) const
    {
        if (offset >= size_ || bytes == 0 || bytes > size_ - offset)
            return;
        const uint32_t first = offset / PageSize;
        const uint32_t last = (offset + bytes - 1) / PageSize;
        for (uint32_t page = first; page <= last; ++page)
        {
            if (code_[page])
                invalidate(page);
        }
    }

private:
    void setCodeMask(uint32_t physical, uint32_t mask)
    {
        const uint32_t previous = code_[physical];
        if (previous == mask)
            return;
        code_[physical] = mask;
        refreshPage(physical);
        if (physical && (!previous || !mask))
            refreshPage(physical - 1);
    }

    uint32_t physicalPage(uintptr_t bias, uint32_t address) const
    {
        const uintptr_t offset = bias + address - main_;
        if ((bias & EeSoftmemIndirectPage) || offset >= size_ || offset % PageSize != 0)
            return None;
        return static_cast<uint32_t>(offset) / PageSize;
    }

    void refreshAlias(uint32_t index, uint32_t physical)
    {
        // A direct unaligned access can straddle the next physical page
        // regardless of how the next guest virtual page is mapped.
        const bool protect = physical != None &&
            (code_[physical] || (physical + 1 < pages_ && code_[physical + 1]));
        entries_[VirtualPages + index] = protect ? EeEncodeWriteGuard(code_[physical]) : entries_[index];
    }

    void refreshPage(uint32_t physical)
    {
        for (uint32_t alias = head_[physical]; alias != None; alias = next_[alias])
            refreshAlias(alias, physical);
    }

    uintptr_t main_;
    uint32_t size_;
    std::span<uintptr_t, 2u * VirtualPages> entries_;
    uint32_t pages_;
    std::unique_ptr<uint32_t[]> next_;
    std::unique_ptr<uint32_t[]> head_;
    std::unique_ptr<uint32_t[]> code_;
};
