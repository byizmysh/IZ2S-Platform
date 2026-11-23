#include "native_pad_frontend.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadDualshock2.h"
#include "IopThread/IopThreadVm.h"
#include <optional>
#include "common/Timer.h"
#include <array>
#include <pthread.h>
namespace {
Ps2NativePad provider{};uintptr_t owner;uint32_t busy,initialized,last_error;
uint32_t input_owned;
std::array<Ps2NativePadFrame, PS2_PAD_PLAYERS> players{};
Ps2NativePadFrame menu_frame{};
unsigned connected_mask;
std::array<uint32_t, PS2_PAD_PLAYERS> devices{};
std::array<unsigned, PS2_PAD_PLAYERS> slots{};
std::array<Ps2PadProfile, PS2_PAD_PLAYERS> profiles{};
unsigned count = 1;
bool assigned;
bool lock(){uint32_t zero=0;return __atomic_compare_exchange_n(&busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
uint32_t done(uint32_t e){last_error=e;__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return e;}
void neutral(unsigned slot) {
    for (unsigned input = 0; input < 16; ++input)
        Pad::SetControllerState(slot, input, 0);
    for (unsigned input = PadDualshock2::PAD_L_UP; input <= PadDualshock2::PAD_R_LEFT; ++input)
        Pad::SetControllerState(slot, input, 0);
}
void neutral_all() {
    for (unsigned player = 0; player < count; ++player) neutral(slots[player]);
}
uint32_t finish_initialize(uint32_t error){
    if(error)return done(error);
    owner=(uintptr_t)pthread_self();initialized=1;
    players = {};
    menu_frame = {};
    connected_mask = 0;
    devices = {};
    slots = {};
    profiles = {};
    count = 1;
    assigned = false;
    __atomic_store_n(&input_owned,0,__ATOMIC_RELEASE);neutral_all();return done(0);
}
uint32_t admitted(){
    if(!initialized)return PS2_PAD_NOT_READY;
    if(owner!=(uintptr_t)pthread_self())return PS2_PAD_OWNER;
    return 0;
}
std::optional<IopThread::VmHold> hold_for_pad_write() {
    if (IopThread::GetMode() == IopThreadMode::Threaded)
        return std::optional<IopThread::VmHold>(std::in_place, IopThread::HoldKind::Lifecycle);
    return std::nullopt;
}
void axes(unsigned slot,unsigned start,int32_t x,int32_t y){
    Pad::SetControllerState(slot,start+0,y<0?(float)-y/32767.f:0.f);
    Pad::SetControllerState(slot,start+1,x>0?(float)x/32767.f:0.f);
    Pad::SetControllerState(slot,start+2,y>0?(float)y/32767.f:0.f);
    Pad::SetControllerState(slot,start+3,x<0?(float)-x/32767.f:0.f);
}
}
extern "C" void ps2_native_pad_set_input_owned(uint32_t value){
    __atomic_store_n(&input_owned,!!value,__ATOMIC_RELEASE);
}
#ifndef PS2_PAD_PORTABLE
extern "C" uint32_t ps2_native_pad_initialize_sdk(uintptr_t sdk,size_t bytes){
    if(!lock())return PS2_PAD_BUSY;if(initialized)return done(PS2_PAD_BUSY);
    if(IopThread::GetMode()!=IopThreadMode::Off)return done(PS2_PAD_IOP_MODE);
    return finish_initialize(ps2_native_pad_bind_sdk(&provider,sdk,bytes));
}
#endif
extern "C" uint32_t ps2_native_pad_initialize_callbacks(const Ps2NativePadApi *api){
    if(!lock())return PS2_PAD_BUSY;if(initialized)return done(PS2_PAD_BUSY);
    if(IopThread::GetMode()!=IopThreadMode::Off)return done(PS2_PAD_IOP_MODE);
    return finish_initialize(ps2_native_pad_bind_callbacks(&provider,api));
}
extern "C" uint32_t ps2_native_pad_initialize_reader(Ps2NativePadReader reader,const void *context){
    if(!lock())return PS2_PAD_BUSY;if(initialized)return done(PS2_PAD_BUSY);
    if(IopThread::GetMode()!=IopThreadMode::Off)return done(PS2_PAD_IOP_MODE);
    return finish_initialize(ps2_native_pad_bind_reader(&provider,reader,context));
}
extern "C" uint32_t ps2_native_pad_configure_players(const unsigned *unified_slots,
                                                     const uint32_t assignments[PS2_PAD_PLAYERS],
                                                     const Ps2PadProfile settings[PS2_PAD_PLAYERS],
                                                     unsigned player_count) {
    if (!unified_slots || !assignments || !settings || !player_count || player_count > PS2_PAD_PLAYERS)
        return PS2_PAD_INVALID;
    unsigned occupied = 0;
    for (unsigned i = 0; i < player_count; ++i) {
        const unsigned slot = unified_slots[i];
        if (slot >= Pad::NUM_CONTROLLER_PORTS || (occupied & (1u << slot)) ||
            assignments[i] >= PS2_PAD_DEVICE_COUNT || settings[i].swap_ab > 1 ||
            settings[i].stick_dpad > 1 || settings[i].deadzone > 2 || settings[i].turbo_cross > 3)
            return PS2_PAD_INVALID;
        occupied |= 1u << slot;
    }
    if (!lock()) return PS2_PAD_BUSY;
    if (const auto error = admitted()) return done(error);
    const auto hold = hold_for_pad_write();
    neutral_all();
    players = {};
    count = player_count;
    assigned = true;
    for (unsigned i = 0; i < count; ++i) {
        slots[i] = unified_slots[i];
        devices[i] = assignments[i];
        profiles[i] = settings[i];
    }
    neutral_all();
    return done(0);
}

static uint32_t player_buttons(const Ps2NativePadFrame& frame, const Ps2PadProfile& profile) {
    uint32_t buttons = frame.buttons;
    constexpr uint32_t cross = 1u << PadDualshock2::PAD_CROSS;
    constexpr uint32_t circle = 1u << PadDualshock2::PAD_CIRCLE;
    if (profile.swap_ab)
        buttons = (buttons & ~(cross | circle)) | (buttons & cross ? circle : 0) |
                  (buttons & circle ? cross : 0);
    if (profile.stick_dpad) {
        static constexpr int32_t zones[] = {6000, 12000, 18000};
        const int32_t zone = zones[profile.deadzone];
        if (frame.lx < -zone) buttons |= 1u << PadDualshock2::PAD_LEFT;
        if (frame.lx > zone) buttons |= 1u << PadDualshock2::PAD_RIGHT;
        if (frame.ly < -zone) buttons |= 1u << PadDualshock2::PAD_UP;
        if (frame.ly > zone) buttons |= 1u << PadDualshock2::PAD_DOWN;
        if ((buttons & 5) == 5) buttons &= ~5u;
        if ((buttons & 10) == 10) buttons &= ~10u;
    }
    if (profile.turbo_cross && (buttons & cross)) {
        const auto half_period = Common::Timer::ConvertMillisecondsToValue(
            100.0 / profile.turbo_cross);
        if (half_period && (Common::Timer::GetCurrentValue() / half_period) % 2)
            buttons &= ~cross;
    }
    return buttons;
}

extern "C" uint32_t ps2_native_pad_poll() {
    if (!lock()) return PS2_PAD_BUSY;
    if (const auto error = admitted()) return done(error);
    const auto error = assigned
        ? ps2_native_pad_read_group(&provider, players.data(), devices.data(), count)
        : ps2_native_pad_read(&provider, &players[0]);
    connected_mask = 0;
    menu_frame = {};
    if (!error) {
        for (unsigned player = 0; player < count; ++player) {
            if (!players[player].connected) continue;
            connected_mask |= 1u << player;
            if (!menu_frame.connected) menu_frame = players[player];
        }
        if (assigned && !menu_frame.connected)
            ps2_native_pad_read(&provider, &menu_frame);
    }
    // Controller state is also read by SIO2 on the IOP owner. Hold it
    // only for guest-state writes, after the HID calls have finished.
    const auto hold = hold_for_pad_write();
    // Keep sampling for the application while its menu owns every guest port.
    if (error || __atomic_load_n(&input_owned, __ATOMIC_ACQUIRE)) {
        neutral_all();
        return done(error);
    }
    for (unsigned player = 0; player < count; ++player) {
        const auto& frame = players[player];
        const auto slot = slots[player];
        const auto* pad = Pad::GetPad(slot);
        if (!pad || pad->GetType() != Pad::ControllerType::DualShock2) continue;
        const uint32_t buttons = player_buttons(frame, profiles[player]);
        for (unsigned input = 0; input < 16; ++input)
            Pad::SetControllerState(slot, input, (buttons & (1u << input)) ? 1.f : 0.f);
        axes(slot, PadDualshock2::PAD_L_UP, frame.lx, frame.ly);
        axes(slot, PadDualshock2::PAD_R_UP, frame.rx, frame.ry);
    }
    return done(0);
}
extern "C" uint32_t ps2_native_pad_shutdown(){
    if(!lock())return PS2_PAD_BUSY;uint32_t e=admitted();if(e)return done(e);
    const auto hold = hold_for_pad_write();
    neutral_all();e=ps2_native_pad_unbind(&provider);if(!e){initialized=0;owner=0;__atomic_store_n(&input_owned,0,__ATOMIC_RELEASE);}return done(e);
}
extern "C" uint32_t ps2_native_pad_statistics(Ps2NativePadStatistics *out){
    if(!out)return PS2_PAD_INVALID;if(!lock())return PS2_PAD_BUSY;
    *out={provider.frame,provider.polls,provider.reads,provider.switches,provider.disconnects,owner,initialized,provider.sdk_verified,provider.max_reads_per_poll,last_error};__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return 0;
}

extern "C" uint32_t ps2_native_pad_menu_sample(Ps2NativePadFrame *frame, unsigned *mask) {
    if (!frame || !mask) return PS2_PAD_INVALID;
    if (!lock()) return PS2_PAD_BUSY;
    *frame = menu_frame;
    *mask = connected_mask;
    __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
    return 0;
}
