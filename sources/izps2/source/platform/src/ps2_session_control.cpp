#include "ps2_session_control.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ps2 {
namespace {
bool IsOperation(Operation op) {
    switch(op) {
    case Operation::Pause: case Operation::Resume: case Operation::Stop:
    case Operation::Save: case Operation::Load: return true;
    }
    return false;
}

bool ValidCheckpoint(Operation op, const std::string& checkpoint) {
    if(op != Operation::Save && op != Operation::Load) return checkpoint.empty();
    return !checkpoint.empty() && checkpoint.size() <= SessionControl::kMaxPathBytes &&
           checkpoint.find('\0') == std::string::npos;
}
} // namespace

SessionControl::SessionControl(SessionState initial, std::size_t capacity)
    : capacity_(capacity), state_(initial) {
    if(capacity == 0 || capacity > kMaxCapacity ||
       (initial != SessionState::Paused && initial != SessionState::Running &&
        initial != SessionState::Stopped)) {
        throw std::invalid_argument("invalid session control configuration");
    }
}

RequestStatus SessionControl::Submit(Operation op, std::uint64_t expected_revision,
                                     std::string checkpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(last_id_ == std::numeric_limits<RequestId>::max()) {
        return {0, op, Outcome::Rejected, Reason::IdExhausted, revision_};
    }
    RequestStatus status{++last_id_, op, Outcome::Queued, Reason::None, revision_};
    if(!IsOperation(op) || !ValidCheckpoint(op, checkpoint)) {
        status.reason = Reason::InvalidArgument;
    } else if(expected_revision != revision_) {
        status.reason = Reason::StaleRevision;
    } else if(!ValidStateLocked(op)) {
        status.reason = Reason::InvalidState;
    } else if(queue_.size() >= capacity_) {
        status.reason = Reason::QueueFull;
    }
    if(status.reason != Reason::None) {
        status.outcome = Outcome::Rejected;
        RememberLocked(status);
    } else {
        queue_.push_back({status, expected_revision, std::move(checkpoint)});
    }
    return status;
}

RequestStatus SessionControl::InspectLocked(RequestId id) const {
    if(has_executing_ && executing_.status.id == id) return executing_.status;
    for(const auto& request : queue_) {
        if(request.status.id == id) return request.status;
    }
    for(const auto& status : history_) {
        if(status.id == id) return status;
    }
    return {id, Operation::Pause, Outcome::Rejected,
            id != 0 && id <= last_id_ ? Reason::StaleRequest : Reason::UnknownRequest,
            revision_};
}

RequestStatus SessionControl::Inspect(RequestId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return InspectLocked(id);
}

RequestStatus SessionControl::Retry(RequestId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto status = InspectLocked(id);
    if(status.reason != Reason::StaleRequest && status.reason != Reason::UnknownRequest) {
        status.outcome = Outcome::Rejected;
        status.reason = Reason::Duplicate;
    }
    return status;
}

SessionSnapshot SessionControl::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {state_, revision_, queue_.size(),
            has_executing_ ? executing_.status.id : 0, unresolved_};
}

bool SessionControl::BindCpuLocked() {
    const auto current = std::this_thread::get_id();
    if(cpu_thread_ == std::thread::id{}) cpu_thread_ = current;
    return cpu_thread_ == current;
}

bool SessionControl::ValidStateLocked(Operation op) const {
    switch(op) {
    case Operation::Pause: return state_ == SessionState::Running;
    case Operation::Resume: case Operation::Save: case Operation::Load:
        return state_ == SessionState::Paused;
    case Operation::Stop: return state_ != SessionState::Stopped;
    }
    return false;
}

void SessionControl::RememberLocked(const RequestStatus& status) {
    if(history_.size() == kHistoryCapacity) history_.pop_front();
    history_.push_back(status);
}

void SessionControl::FinishLocked(CallbackResult result) {
    if(result != CallbackResult::Succeeded && result != CallbackResult::FailedNoChange &&
       result != CallbackResult::FailedChanged && result != CallbackResult::SucceededWithWarning) {
        executing_.status.reason = Reason::OutcomeUnknown;
        unresolved_ = true;
        return;
    }
    auto& status = executing_.status;
    if(result == CallbackResult::Succeeded || result == CallbackResult::SucceededWithWarning) {
        switch(status.operation) {
        case Operation::Pause: state_ = SessionState::Paused; break;
        case Operation::Resume: state_ = SessionState::Running; break;
        case Operation::Stop: state_ = SessionState::Stopped; break;
        case Operation::Save: case Operation::Load: break;
        }
        ++revision_;
        status.outcome = Outcome::Succeeded;
        status.reason = result == CallbackResult::SucceededWithWarning ? Reason::PersistenceFailed : Reason::None;
    } else {
        if(result == CallbackResult::FailedChanged) ++revision_;
        status.outcome = Outcome::Failed;
        status.reason = Reason::CallbackFailed;
    }
    status.revision = revision_;
    RememberLocked(status);
    has_executing_ = false;
    unresolved_ = false;
    executing_ = {};
    if(state_ == SessionState::Stopped) {
        for(auto& request : queue_) {
            request.status.outcome = Outcome::Cancelled;
            request.status.reason = Reason::Stopped;
            request.status.revision = revision_;
            RememberLocked(request.status);
        }
        queue_.clear();
    }
}

ProcessResult SessionControl::ProcessOne(const SessionCallbacks& callbacks) {
    Operation op;
    const std::string* checkpoint = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!BindCpuLocked()) return ProcessResult::WrongThread;
        if(has_executing_) return ProcessResult::Unresolved;
        if(queue_.empty()) return ProcessResult::Idle;
        auto request = std::move(queue_.front());
        queue_.pop_front();
        Reason rejection = Reason::None;
        if(request.expected_revision != revision_) rejection = Reason::StaleRevision;
        else if(!ValidStateLocked(request.status.operation)) rejection = Reason::InvalidState;
        else if(revision_ == std::numeric_limits<std::uint64_t>::max()) rejection = Reason::IdExhausted;
        if(rejection != Reason::None) {
            request.status.outcome = Outcome::Rejected;
            request.status.reason = rejection;
            request.status.revision = revision_;
            RememberLocked(request.status);
            return ProcessResult::Completed;
        }
        request.status.outcome = Outcome::Executing;
        request.status.revision = revision_;
        executing_ = std::move(request);
        has_executing_ = true;
        op = executing_.status.operation;
        checkpoint = &executing_.checkpoint;
    }

    CallbackResult result = CallbackResult::FailedNoChange;
    bool available = false;
    try {
        switch(op) {
        case Operation::Pause:
            available = static_cast<bool>(callbacks.pause);
            if(available) result = callbacks.pause();
            break;
        case Operation::Resume:
            available = static_cast<bool>(callbacks.resume);
            if(available) result = callbacks.resume();
            break;
        case Operation::Stop:
            available = static_cast<bool>(callbacks.stop);
            if(available) result = callbacks.stop();
            break;
        case Operation::Save:
            available = static_cast<bool>(callbacks.save);
            if(available) result = callbacks.save(*checkpoint);
            break;
        case Operation::Load:
            available = static_cast<bool>(callbacks.load);
            if(available) result = callbacks.load(*checkpoint);
            break;
        }
    } catch(...) {
        result = CallbackResult::Unknown;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    FinishLocked(result);
    if(!available) history_.back().reason = Reason::CallbackUnavailable;
    return unresolved_ ? ProcessResult::Unresolved : ProcessResult::Completed;
}

ProcessResult SessionControl::ResolveExecuting(RequestId id, CallbackResult result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!BindCpuLocked()) return ProcessResult::WrongThread;
    if(!has_executing_ || !unresolved_ || executing_.status.id != id) {
        return has_executing_ ? ProcessResult::Unresolved : ProcessResult::Idle;
    }
    FinishLocked(result);
    return unresolved_ ? ProcessResult::Unresolved : ProcessResult::Completed;
}

} // namespace ps2
