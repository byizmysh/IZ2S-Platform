#pragma once

#include <cstdint>

namespace IopThread { class Thread; }

namespace Ps2IopJitMailbox {
using PublishFn = std::uint32_t (*)(void*, std::uint32_t offset, std::uint32_t length);

enum class Outcome {
    Completed,
    Rejected,
    TimedOutBeforePublication,
    CompletionUnknown
};

struct Result {
    Outcome outcome;
    std::uint32_t sdk_result;
};

// The EE attaches before Start and detaches after Stop. Unknown completion
// keeps the binding and command alive until the owner has actually finished.
bool Attach(IopThread::Thread& scheduler, PublishFn publish, void* context);
bool Detach(IopThread::Thread& scheduler);
Result PublishWhileParked(IopThread::Thread& scheduler, std::uint32_t offset,
                          std::uint32_t bytes, std::uint64_t timeout_ns);

// These run only on the IOP scheduler's held paths.
bool Pending(const IopThread::Thread& scheduler);
void Service(const IopThread::Thread& scheduler);
}
