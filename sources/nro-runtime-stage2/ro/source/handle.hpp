// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>

namespace ounce {

/// Owns one kernel handle. Operations that can fail remain explicit elsewhere.
class HandleOwner {
  public:
    HandleOwner() = default;
    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;

    ~HandleOwner() {
        reset();
    }

    Handle get() const {
        return handle_;
    }

    void reset(Handle handle = INVALID_HANDLE) {
        if (handle_ != INVALID_HANDLE) {
            svcCloseHandle(handle_);
        }
        handle_ = handle;
    }

  private:
    Handle handle_ = INVALID_HANDLE;
};

} // namespace ounce
