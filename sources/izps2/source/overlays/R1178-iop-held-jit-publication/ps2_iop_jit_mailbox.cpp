#include "ps2_iop_jit_mailbox.hpp"

#include "IopThread/IopThread.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <thread>

namespace Ps2IopJitMailbox {
namespace {
enum class State { Idle, Posted, Publishing, Done };

struct Mailbox {
    std::atomic<const IopThread::Thread*> scheduler{nullptr};
    std::atomic<State> state{State::Idle};
    std::atomic<std::thread::id> ee_owner{};
    PublishFn publish = nullptr;
    void* context = nullptr;
    std::uint32_t offset = 0, bytes = 0, sdk_result = 0;
};
Mailbox mailbox;

bool IsBound(const IopThread::Thread& scheduler)
{
    return mailbox.scheduler.load(std::memory_order_acquire) == &scheduler;
}

bool IsProducer()
{
    return mailbox.ee_owner.load(std::memory_order_relaxed) == std::this_thread::get_id();
}

Result TakeResult()
{
    const std::uint32_t sdk_result = mailbox.sdk_result;
    mailbox.state.store(State::Idle, std::memory_order_release);
    return {Outcome::Completed, sdk_result};
}
}

bool Attach(IopThread::Thread& scheduler, PublishFn publish, void* context)
{
    if (!publish || scheduler.IsRunning() ||
        mailbox.scheduler.load(std::memory_order_acquire) != nullptr)
        return false;
    mailbox.publish = publish;
    mailbox.context = context;
    mailbox.ee_owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
    mailbox.scheduler.store(&scheduler, std::memory_order_release);
    return true;
}

bool Detach(IopThread::Thread& scheduler)
{
    if (!IsBound(scheduler) || !IsProducer() || scheduler.IsRunning())
        return false;
    const State state = mailbox.state.load(std::memory_order_acquire);
    if (state == State::Posted || state == State::Publishing)
        return false;
    mailbox.state.store(State::Idle, std::memory_order_relaxed);
    mailbox.publish = nullptr;
    mailbox.context = nullptr;
    mailbox.ee_owner.store({}, std::memory_order_relaxed);
    mailbox.scheduler.store(nullptr, std::memory_order_release);
    return true;
}

bool Pending(const IopThread::Thread& scheduler)
{
    return IsBound(scheduler) && mailbox.state.load(std::memory_order_acquire) == State::Posted;
}

void Service(const IopThread::Thread& scheduler)
{
    if (!IsBound(scheduler))
        return;
    State state = State::Posted;
    if (!mailbox.state.compare_exchange_strong(state, State::Publishing,
            std::memory_order_acquire, std::memory_order_relaxed))
        return;
    mailbox.sdk_result = mailbox.publish(mailbox.context, mailbox.offset, mailbox.bytes);
    mailbox.state.store(State::Done, std::memory_order_release);
}

Result PublishWhileParked(IopThread::Thread& scheduler, std::uint32_t offset,
                          std::uint32_t bytes, std::uint64_t timeout_ns)
{
    if (!IsBound(scheduler) || !IsProducer() || !scheduler.IsRunning() ||
        scheduler.Mode() != IopThreadMode::Threaded || !scheduler.IopIsStopped() ||
        !bytes || bytes > std::numeric_limits<std::uint32_t>::max() - offset ||
        mailbox.state.load(std::memory_order_acquire) != State::Idle)
        return {Outcome::Rejected, 0};

    mailbox.offset = offset;
    mailbox.bytes = bytes;
    mailbox.state.store(State::Posted, std::memory_order_release);
    scheduler.Wake();

    const auto start = std::chrono::steady_clock::now();
    const auto limit = std::chrono::nanoseconds{
        std::min(timeout_ns, std::uint64_t(std::numeric_limits<std::int64_t>::max()))};
    for (;;) {
        if (mailbox.state.load(std::memory_order_acquire) == State::Done)
            return TakeResult();
        if (std::chrono::steady_clock::now() - start >= limit)
            break;
        std::this_thread::yield();
    }

    State state = State::Posted;
    if (mailbox.state.compare_exchange_strong(state, State::Idle,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return {Outcome::TimedOutBeforePublication, 0};
    if (state == State::Done)
        return TakeResult();
    return {Outcome::CompletionUnknown, 0};
}
}
