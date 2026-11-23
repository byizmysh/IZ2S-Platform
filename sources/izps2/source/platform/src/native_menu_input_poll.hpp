#pragma once

#include "ps2_app_controller.h"

namespace ps2 {

class NativeMenuInputPoll {
public:
    template <class Poll>
    bool Read(std::uint64_t now_ms, AppControllerInput* out, Poll poll)
    {
        // Keep input discovery, menu transitions and failed polls responsive.
        // Gameplay also polls through InputManager on the original VSync path.
        if (valid_ && now_ms >= last_ms_ && now_ms - last_ms_ < 4) {
            *out = input_;
            return true;
        }
        valid_ = false;
        if (!poll(out))
            return false;
        input_ = *out;
        last_ms_ = now_ms;
        valid_ = input_.connected;
        return true;
    }

    void Invalidate() { valid_ = false; }

private:
    AppControllerInput input_{};
    std::uint64_t last_ms_ = 0;
    bool valid_ = false;
};

} // namespace ps2
