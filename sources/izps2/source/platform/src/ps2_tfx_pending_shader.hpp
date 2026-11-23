#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// A renderer keeps the finished handle in its own cache. This tracks only
// active builds and shares their result with callers already waiting for it.
template<typename Key, typename Module>
class Ps2PendingShaders
{
public:
    template<typename Build>
    Module Get(const void* owner, const Key& key, const Build& build)
    {
        std::unique_lock lock(m_mutex);
        const auto existing = std::find_if(m_pending.begin(), m_pending.end(), [&](const auto& item) {
            return item->owner == owner && item->key == key;
        });
        if (existing != m_pending.end())
        {
            const auto request = *existing;
            ++m_joined;
            request->ready.wait(lock, [&] { return request->complete; });
            return request->module;
        }

        const auto request = std::make_shared<Request>(owner, key);
        m_pending.push_back(request);
        lock.unlock();

        const Module built = build();

        lock.lock();
        request->module = built;
        request->complete = true;
        m_pending.erase(std::find(m_pending.begin(), m_pending.end(), request));
        lock.unlock();
        request->ready.notify_all();
        return built;
    }

    std::uint64_t TakeJoinedCount()
    {
        std::unique_lock lock(m_mutex);
        const auto joined = m_joined;
        m_joined = 0;
        return joined;
    }

private:
    struct Request
    {
        Request(const void* owner_arg, const Key& key_arg) : owner(owner_arg), key(key_arg) {}
        const void* owner;
        Key key;
        std::condition_variable ready;
        bool complete = false;
        Module module{};
    };

    std::mutex m_mutex;
    std::vector<std::shared_ptr<Request>> m_pending;
    std::uint64_t m_joined = 0;
};
