#include "ps2_app_controller.h"
#include "ps2_session_control.h"

#include <cstring>

namespace ps2 {
namespace {
constexpr std::uint32_t kUp=1u<<0, kDown=1u<<2, kCircle=1u<<5, kCross=1u<<6;
constexpr std::uint32_t kOpenChord=(1u<<14)|(1u<<15);
constexpr std::uint32_t kR3=1u<<15;
constexpr std::uint32_t kMenu=1u<<16;
constexpr std::uint64_t kDoubleR3Ms=350;
bool IsState(const Ps2SessionStatus& status, SessionState state) {
    return status.state==static_cast<std::uint32_t>(state);
}
bool IsOutcome(const Ps2SessionStatus& status, Outcome outcome) {
    return status.outcome==static_cast<std::uint32_t>(outcome);
}
bool IsReason(const Ps2SessionStatus& status, Reason reason) {
    return status.reason==static_cast<std::uint32_t>(reason);
}
} // namespace

AppController::AppController(AppControllerHost host, AppControllerConfig config)
    : host_(host), hold_ms_(config.open_hold_ms) {
    std::size_t length=0;
    if(config.checkpoint)while(length<checkpoint_.size() && config.checkpoint[length])++length;
    const bool valid_path=length>0 && length<checkpoint_.size() &&
        !std::strchr(config.checkpoint,'/') && !std::strchr(config.checkpoint,'\\') &&
        length>=5 && std::strcmp(config.checkpoint+length-4,".p2s")==0;
    bool valid_text=valid_path;
    for(std::size_t i=0;valid_text && i<length;i++)
        valid_text=static_cast<unsigned char>(config.checkpoint[i])>=32 &&
            static_cast<unsigned char>(config.checkpoint[i])!=127;
    enabled_=host_.poll_input && host_.suppress_gameplay_input && host_.publish_view &&
        valid_text && hold_ms_>0 && hold_ms_<=5000;
    if(enabled_){std::memcpy(checkpoint_.data(),config.checkpoint,length+1);view_.message=config.initial_message;}
    else view_.message=AppControllerMessage::Failed;
    Publish();
}

void AppController::Publish() {
    if(dirty_ && host_.publish_view)host_.publish_view(host_.context,&view_);
    dirty_=false;
}

void AppController::MarkMessage(AppControllerMessage message) {
    if(view_.message!=message){view_.message=message;dirty_=true;}
}

void AppController::Submit(unsigned operation, const Ps2SessionStatus& status) {
    // One in-flight ID. Never resubmit to recover a delayed/missing receipt.
    if(pending_ || unresolved_ || stopped_)return;
    const char* path=(operation==PS2_SESSION_SAVE || operation==PS2_SESSION_LOAD)?checkpoint_.data():nullptr;
    const auto id=host_.request?host_.request(host_.context,operation,status.revision,path):
        ps2_session_request(operation,status.revision,path);
    view_.operation=operation;view_.confirming=false;release_gate_=true;dirty_=true;
    if(!id){exit_requested_=false;MarkMessage(AppControllerMessage::Failed);return;}
    pending_=id;pending_operation_=operation;view_.request_id=id;view_.waiting=true;
    view_.outcome=static_cast<std::uint32_t>(Outcome::Queued);
    view_.reason=static_cast<std::uint32_t>(Reason::None);
    MarkMessage(AppControllerMessage::None);
}

bool AppController::Acknowledge(Ps2SessionStatus* status) {
    const int available=host_.status?host_.status(host_.context,pending_,status):
        ps2_session_status(pending_,status);
    if(!available){
        unresolved_=true;MarkMessage(AppControllerMessage::Unknown);
        return false;
    }
    if(!IsState(*status,SessionState::Running) && !IsState(*status,SessionState::Paused) &&
       !IsState(*status,SessionState::Stopped)){
        unresolved_=true;MarkMessage(AppControllerMessage::Unknown);return false;
    }
    if(!pending_){ // request_id=0 is used only for the live snapshot.
        unresolved_=status->unresolved!=0;
        if(unresolved_)MarkMessage(AppControllerMessage::Unknown);
        else if(view_.message==AppControllerMessage::Unknown)MarkMessage(AppControllerMessage::None);
        if((unresolved_ || IsState(*status,SessionState::Paused)) && !want_menu_){
            want_menu_=true;view_.visible=true;release_gate_=true;dirty_=true;
        }
        return true;
    }
    if(status->request_id!=pending_){unresolved_=true;MarkMessage(AppControllerMessage::Unknown);return true;}
    if(view_.outcome!=status->outcome || view_.reason!=status->reason){
        view_.outcome=status->outcome;view_.reason=status->reason;dirty_=true;
    }
    if(status->unresolved || IsReason(*status,Reason::OutcomeUnknown) ||
       IsReason(*status,Reason::UnknownRequest) || IsReason(*status,Reason::StaleRequest)){
        unresolved_=true;MarkMessage(AppControllerMessage::Unknown);return true;
    }
    if(IsOutcome(*status,Outcome::Queued) || IsOutcome(*status,Outcome::Executing)){
        // A later valid receipt may recover a temporarily unavailable status.
        unresolved_=false;
        if(view_.message==AppControllerMessage::Unknown)MarkMessage(AppControllerMessage::None);
        return true;
    }
    if(!IsOutcome(*status,Outcome::Succeeded) && !IsOutcome(*status,Outcome::Failed) &&
       !IsOutcome(*status,Outcome::Rejected) && !IsOutcome(*status,Outcome::Cancelled)){
        unresolved_=true;MarkMessage(AppControllerMessage::Unknown);return true;
    }
    const bool succeeded=IsOutcome(*status,Outcome::Succeeded);
    const auto expected_state=pending_operation_==PS2_SESSION_RESUME?SessionState::Running:
        pending_operation_==PS2_SESSION_STOP?SessionState::Stopped:SessionState::Paused;
    if(succeeded && !IsState(*status,expected_state)){
        unresolved_=true;MarkMessage(AppControllerMessage::Unknown);return true;
    }
    pending_=0;unresolved_=false;view_.waiting=false;view_.confirming=false;
    release_gate_=true;dirty_=true;
    if(!succeeded){
        exit_requested_=false;
        want_menu_=true;view_.visible=true;MarkMessage(AppControllerMessage::Failed);return true;
    }
    switch(pending_operation_){
    case PS2_SESSION_PAUSE:
        want_menu_=true;view_.visible=true;
        MarkMessage(IsReason(*status,Reason::PersistenceFailed)?AppControllerMessage::Failed:AppControllerMessage::None);break;
    case PS2_SESSION_RESUME:
        want_menu_=false;view_.visible=false;drain_until_release_=true;
        MarkMessage(AppControllerMessage::None);break;
    case PS2_SESSION_SAVE:MarkMessage(AppControllerMessage::Saved);break;
    case PS2_SESSION_LOAD:MarkMessage(AppControllerMessage::Loaded);break;
    case PS2_SESSION_STOP:
        stopped_=true;exit_requested_=false;want_menu_=false;view_.visible=false;drain_until_release_=true;break;
    }
    return true;
}

void AppController::RequestExit() {
    if(!enabled_ || stopped_ || exit_requested_)return;
    if(host_.request_quit)host_.request_quit(host_.context);
    exit_requested_=true;want_menu_=true;view_.visible=true;view_.selected=3;
    view_.confirming=false;release_gate_=true;dirty_=true;
    if(!unresolved_)MarkMessage(AppControllerMessage::None);
}

void AppController::Tick(std::uint64_t monotonic_ms) {
    if(!enabled_)return;
    if(monotonic_ms<previous_ms_){
        drain_until_release_=drain_until_release_||chord_active_;chord_active_=false;
        r3_first_armed_=false;
    }
    previous_ms_=monotonic_ms;
    Ps2SessionStatus status{};
    const bool have_status=Acknowledge(&status);
    if(have_status && IsState(status,SessionState::Stopped)){
        if(!stopped_ || view_.visible || view_.confirming)dirty_=true;
        stopped_=true;want_menu_=false;view_.visible=false;view_.confirming=false;
    }
    AppControllerInput input{};
    const bool connected=host_.poll_input(host_.context,&input) && input.connected;
    const bool connection_changed=view_.input_connected!=connected;
    if(connection_changed){
        view_.input_connected=connected;dirty_=true;release_gate_=true;
        r3_first_armed_=false;
        if(!connected && want_menu_)MarkMessage(AppControllerMessage::Failed);
    }
    if(!connected){
        input.buttons=0;drain_until_release_=true;chord_active_=false;
    }
    const auto buttons=input.buttons;
    const auto pressed=buttons&~previous_buttons_;
    previous_buttons_=buttons;
    if(connected && buttons==0){
        chord_consumed_=false;drain_until_release_=false;
    }
    const bool chord=(buttons&kOpenChord)==kOpenChord;
    const bool menu_pressed=(pressed&kMenu)&&!connection_changed;
    // The deliberate L3+R3 pause chord wins over double-click detection. The
    // first standalone R3 is forwarded normally; no delayed/synthetic replay.
    if(!connected || connection_changed || chord || menu_pressed){
        r3_first_armed_=false;
    }else if(!stopped_ && !unresolved_){
        if(r3_first_armed_ && monotonic_ms-r3_first_ms_>kDoubleR3Ms)r3_first_armed_=false;
        if(pressed&kR3){
            if(r3_first_armed_){
                r3_first_armed_=false;RequestExit();
            }else{
                r3_first_armed_=true;r3_first_ms_=monotonic_ms;
            }
        }
    }
    if(exit_requested_ && !pending_ && !unresolved_ && !stopped_){
        if(!have_status){exit_requested_=false;MarkMessage(AppControllerMessage::Failed);}
        else if(IsState(status,SessionState::Running))Submit(PS2_SESSION_PAUSE,status);
        else if(IsState(status,SessionState::Paused))Submit(PS2_SESSION_STOP,status);
        else {exit_requested_=false;MarkMessage(AppControllerMessage::Failed);}
    }
    if(!want_menu_ && !pending_ && !unresolved_ && !stopped_ && connected){
        if((chord && !chord_consumed_) || menu_pressed){
            if(!chord_active_){chord_active_=true;chord_started_ms_=monotonic_ms;}
            if((menu_pressed || monotonic_ms-chord_started_ms_>=hold_ms_) && have_status){
                chord_active_=false;chord_consumed_=true;want_menu_=true;view_.visible=true;
                view_.selected=0;view_.confirming=false;release_gate_=true;dirty_=true;
                MarkMessage(AppControllerMessage::None);
                if(IsState(status,SessionState::Running))Submit(PS2_SESSION_PAUSE,status);
                else if(!IsState(status,SessionState::Paused))MarkMessage(AppControllerMessage::Failed);
            }
        }else if(chord_active_){
            chord_active_=false;drain_until_release_=buttons!=0;
        }
    }
    claims_input_=want_menu_ || pending_ || unresolved_ || stopped_ || chord_active_ ||
        drain_until_release_ || !connected;
    if(claims_input_)host_.suppress_gameplay_input(host_.context);
    // Paused operation uses this same host-poll path. VSync is not required.
    if(!want_menu_ || pending_ || unresolved_ || stopped_ || !connected || !have_status){Publish();return;}
    if(release_gate_){if(buttons==0)release_gate_=false;Publish();return;}
    if(view_.confirming){
        if(pressed&kCircle){view_.confirming=false;release_gate_=true;dirty_=true;}
        else if(pressed&kCross){
            if(IsState(status,SessionState::Paused))Submit(view_.selected==2?PS2_SESSION_LOAD:PS2_SESSION_STOP,status);
            else {view_.confirming=false;MarkMessage(AppControllerMessage::Failed);}
        }
        Publish();return;
    }
    if((pressed&kUp) && !(pressed&kDown)){
        view_.selected=(view_.selected+3)%4;MarkMessage(AppControllerMessage::None);dirty_=true;
    }else if((pressed&kDown) && !(pressed&kUp)){
        view_.selected=(view_.selected+1)%4;MarkMessage(AppControllerMessage::None);dirty_=true;
    }else if(pressed&(kCircle|kCross)){
        const unsigned selected=(pressed&kCircle)?0:view_.selected;
        if(selected==0){
            if(IsState(status,SessionState::Paused))Submit(PS2_SESSION_RESUME,status);
            else if(IsState(status,SessionState::Running)){
                // Pause failure left the VM running: explicit Resume closes the
                // menu without submitting an invalid or duplicate operation.
                want_menu_=false;view_.visible=false;drain_until_release_=true;dirty_=true;
            }else MarkMessage(AppControllerMessage::Failed);
        }else if(!IsState(status,SessionState::Paused))MarkMessage(AppControllerMessage::Failed);
        else if(selected==1)Submit(PS2_SESSION_SAVE,status);
        else {view_.confirming=true;release_gate_=true;MarkMessage(AppControllerMessage::None);dirty_=true;}
    }
    Publish();
}
} // namespace ps2
