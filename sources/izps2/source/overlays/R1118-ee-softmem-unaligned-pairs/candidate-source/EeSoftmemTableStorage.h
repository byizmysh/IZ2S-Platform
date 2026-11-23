#pragma once

#include "EeCodeWriteIndex.h"

class EeSoftmemTableStorage
{
public:
    static constexpr uint32_t Pages = EeCodeWriteIndex::VirtualPages;
    static constexpr uint32_t PageSize = EeCodeWriteIndex::PageSize;

    explicit EeSoftmemTableStorage(std::span<uintptr_t, Pages> canonical) : m_canonical(canonical) {}

    bool activate(uintptr_t main, uint32_t bytes)
    {
        if (m_planes)
            return false;
        auto planes = std::unique_ptr<uintptr_t[]>(new (std::nothrow) uintptr_t[2u * Pages]);
        if (!planes)
            return false;
        std::copy(m_canonical.begin(), m_canonical.end(), planes.get());
        auto index = std::unique_ptr<EeCodeWriteIndex>(new (std::nothrow)
            EeCodeWriteIndex(main, bytes, std::span<uintptr_t, 2u * Pages>(planes.get(), 2u * Pages)));
        if (!index || !index->ready())
            return false;
        m_planes = std::move(planes);
        m_index = std::move(index);
        return true;
    }

    const uintptr_t* readBase() const { return m_index ? m_planes.get() : m_canonical.data(); }
    EeCodeWriteIndex* index() const { return m_index.get(); }

    void setVirtualPage(uint32_t address, uintptr_t tag)
    {
        const uint32_t page = address / PageSize;
        m_canonical[page] = tag;
        if (m_index)
            m_index->setVirtualPage(address, tag);
        else if (m_planes)
        {
            m_planes[page] = tag;
            m_planes[Pages + page] = tag;
        }
    }

    // The generated dispatcher may already hold this table's address after
    // a VM reset. Keep both planes mapped through the rest of the VM session.
    void retireTracking()
    {
        if (m_index)
            m_index->resetCodePages();
        m_index.reset();
    }

    void shutdown()
    {
        m_index.reset();
        m_planes.reset();
    }

private:
    std::span<uintptr_t, Pages> m_canonical;
    std::unique_ptr<uintptr_t[]> m_planes;
    std::unique_ptr<EeCodeWriteIndex> m_index;
};
