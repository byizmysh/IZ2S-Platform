#ifndef PS2_APP_CONTROLLER_H
#define PS2_APP_CONTROLLER_H

#include "ps2_session_frontend.h"
#include <array>
#include <cstdint>

namespace ps2 {

// Input uses the original DualShock2 bind indices (Up=0, Down=2, Circle=5,
// Cross=6, L3=14, R3=15), not the PS2 serial-protocol button bit ordering.
struct AppControllerInput { std::uint32_t buttons = 0; bool connected = false; };
enum class AppControllerMessage : std::uint32_t {
    None = 0, Saved = 1, Loaded = 2, Failed = 3, Unknown = 4
};
struct AppControllerView {
    bool visible = false, waiting = false, confirming = false;
    std::uint32_t selected = 0; // Resume, Save State, Load State, Save and Exit
    AppControllerMessage message = AppControllerMessage::None;
    bool input_connected = false;
    std::uint64_t request_id = 0;
    std::uint32_t operation = PS2_SESSION_PAUSE, outcome = 0, reason = 0;
};
struct AppControllerHost {
    void* context = nullptr;
    // Called at EVERY Tick, including while the guest is paused. May use an
    // already-initialized ordinary Pad provider; this class acquires no device.
    bool (*poll_input)(void*, AppControllerInput*) = nullptr;
    // Called after polling and before returning whenever the application owns
    // input. Clear ALL guest buttons/axes before the next guest slice.
    void (*suppress_gameplay_input)(void*) = nullptr;
    // Plain snapshot, published only on changes. Required so menu actions are
    // never invisible. The receiver copies it if rendering on another thread.
    void (*publish_view)(void*, const AppControllerView*) = nullptr;
    // Optional quick-exit latch: CPU shutdown remains on the safe-point path.
    void (*request_quit)(void*) = nullptr;
    // Optional test/host adapters. Null uses the public in-process session API.
    std::uint64_t (*request)(void*, unsigned, std::uint64_t, const char*) = nullptr;
    int (*status)(void*, std::uint64_t, Ps2SessionStatus*) = nullptr;
};
struct AppControllerConfig {
    const char* checkpoint = nullptr; // caller's game-specific private filename
    std::uint32_t open_hold_ms = 600;
    AppControllerMessage initial_message = AppControllerMessage::None;
};

// Construct and Tick on the application CPU owner thread, at a safe point
// BEFORE SessionControl::ProcessOne and before every guest execution slice.
// The paused loop must keep calling Tick (e.g. every 5 ms); guest VSync cannot
// poll input while paused. Tick does not sleep or run guest CPU/GS operations.
// Minus (reserved bit 16) or holding L3+R3 pauses/opens the emulator menu;
// release all buttons; Up/Down selects, Cross acts,
// Circle resumes/cancels confirmation. Load and Save-and-Exit require a second
// fresh Cross after release. Stop means the main callback's Save-and-Exit flow.
// Two fresh R3 presses with release between them, at most 350 ms apart, request
// exit directly, without the menu confirmation. The native quick-exit
// callback skips creating an autosave; the normal menu retains Save-and-Exit. A single R3 passes to
// the game immediately; the second press is suppressed before guest execution.
// Minus and the L3+R3 chord cancel double-R3 detection. Plus reaches guest Start.
class AppController {
public:
    AppController(AppControllerHost, AppControllerConfig);
    void Tick(std::uint64_t monotonic_ms);
    // CPU-owner request, also used by the independent launcher observer.
    // Uses the same acknowledged pause/stop queue as the physical gesture.
    void RequestExit();
    bool Enabled() const { return enabled_; }
    bool ClaimsInput() const { return claims_input_; }
    const AppControllerView& View() const { return view_; }

private:
    void Publish();
    void Submit(unsigned operation, const Ps2SessionStatus&);
    bool Acknowledge(Ps2SessionStatus*);
    void MarkMessage(AppControllerMessage);
    AppControllerHost host_;
    std::array<char, 257> checkpoint_{};
    AppControllerView view_{};
    bool enabled_ = false, dirty_ = true, claims_input_ = false;
    bool chord_active_ = false, chord_consumed_ = false, release_gate_ = true;
    bool drain_until_release_ = false;
    bool r3_first_armed_ = false, exit_requested_ = false;
    bool want_menu_ = false, unresolved_ = false, stopped_ = false;
    std::uint32_t previous_buttons_ = 0, hold_ms_ = 600;
    std::uint64_t chord_started_ms_ = 0, previous_ms_ = 0, pending_ = 0;
    std::uint64_t r3_first_ms_ = 0;
    unsigned pending_operation_ = PS2_SESSION_PAUSE;
};

} // namespace ps2
#endif
