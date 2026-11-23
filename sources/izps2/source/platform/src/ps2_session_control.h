#ifndef PS2_SESSION_CONTROL_H
#define PS2_SESSION_CONTROL_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ps2 {

using RequestId = std::uint64_t;
enum class Operation { Pause, Resume, Stop, Save, Load };
enum class SessionState { Paused, Running, Stopped };
enum class Outcome { Queued, Executing, Succeeded, Failed, Rejected, Cancelled };
enum class Reason {
    None, InvalidArgument, QueueFull, StaleRevision, InvalidState,
    Duplicate, StaleRequest, UnknownRequest, CallbackUnavailable,
    CallbackFailed, OutcomeUnknown, Stopped, IdExhausted, PersistenceFailed
};
enum class CallbackResult { Succeeded, FailedNoChange, Unknown, FailedChanged,
                            SucceededWithWarning };
enum class ProcessResult { Idle, Completed, Unresolved, WrongThread };

struct RequestStatus {
    RequestId id = 0;
    Operation operation = Operation::Pause;
    Outcome outcome = Outcome::Rejected;
    Reason reason = Reason::UnknownRequest;
    std::uint64_t revision = 0;
};

struct SessionSnapshot {
    SessionState state = SessionState::Paused;
    std::uint64_t revision = 0;
    std::size_t queued = 0;
    RequestId executing = 0;
    bool unresolved = false;
};

// These synchronous callbacks run only on the bound CPU thread at its safe point.
// FailedNoChange guarantees no guest change. FailedChanged acknowledges a
// completed failure with a usable VM (e.g. an original LoadState reset), and
// advances the revision so queued requests cannot act on the old guest state.
// An exception or an unusable/uncertain VM is treated as Unknown.
// SucceededWithWarning completes the state transition but reports a failed
// persistence side effect. The known paused VM remains usable for retry/resume.
// The adapter validates checkpoint scope and performs the actual core operation.
struct SessionCallbacks {
    std::function<CallbackResult()> pause;
    std::function<CallbackResult()> resume;
    std::function<CallbackResult()> stop;
    std::function<CallbackResult(const std::string&)> save;
    std::function<CallbackResult(const std::string&)> load;
};

class SessionControl {
public:
    static constexpr std::size_t kMaxPathBytes = 256;
    static constexpr std::size_t kMaxCapacity = 64;
    static constexpr std::size_t kHistoryCapacity = 64;

    // Construct only after real VM initialization. Capacity must be in [1, 64].
    explicit SessionControl(SessionState initial = SessionState::Paused,
                            std::size_t capacity = 16);

    // Producer API: every fresh submission gets a strictly increasing ID,
    // including rejected submissions. All requests require an exact revision.
    RequestStatus Submit(Operation, std::uint64_t expected_revision,
                         std::string checkpoint = {});
    RequestStatus Inspect(RequestId) const;
    // A lost acknowledgement is recovered through Inspect/Retry, never resubmit.
    // Retry rejects every previously assigned ID and cannot execute an operation.
    RequestStatus Retry(RequestId) const;
    SessionSnapshot Snapshot() const;

    // First CPU call binds the consumer permanently to that calling thread.
    // Invoke between VM slices, with core access exclusively owned by that thread.
    ProcessResult ProcessOne(const SessionCallbacks&);
    // Unknown callback outcomes keep the request Executing and block the queue.
    // Resolve only after the CPU adapter independently establishes the outcome.
    ProcessResult ResolveExecuting(RequestId, CallbackResult);

private:
    struct Request {
        RequestStatus status;
        std::uint64_t expected_revision = 0;
        std::string checkpoint;
    };
    bool BindCpuLocked();
    bool ValidStateLocked(Operation) const;
    RequestStatus InspectLocked(RequestId) const;
    void RememberLocked(const RequestStatus&);
    void FinishLocked(CallbackResult);

    mutable std::mutex mutex_;
    const std::size_t capacity_;
    SessionState state_;
    std::uint64_t revision_ = 0;
    RequestId last_id_ = 0;
    std::deque<Request> queue_;
    std::deque<RequestStatus> history_;
    Request executing_;
    bool has_executing_ = false;
    bool unresolved_ = false;
    std::thread::id cpu_thread_;
};

} // namespace ps2
#endif
