#include "native_av_bindings.h"
#include "native_audio_stream.h"
/* VM owner binds TLS/once/SDK threads before C++ constructors and this install.
 * Binding and arena must remain resident until stream close AND worker join.
 * AudioStream itself retains/parks on uncertain SDK ownership or failed join.
 */
static void retained(void*p,int error){auto*b=static_cast<Ps2SdkAudioBinding*>(p);b->retained=1;b->last_sdk_error=static_cast<uint32_t>(error);}
extern "C" int ps2_sdk_audio_install_stream(Ps2SdkAudioBinding*b){
    if(!b||!b->verified||b->live||b->retained)return PS2_AV_INVALID;
    Ps2LiveAudioHost host{};host.audio=b->audio;host.context=b;
    host.start_worker=ps2_sdk_audio_start_worker;host.join_worker=ps2_sdk_audio_join_worker;
    host.sleep_ns=b->audio.sleep_ns;host.retained=retained;host.pull_frames=1024;host.control_wait_ms=3000;host.drain_wait_ms=3000;
    return ps2_live_audio_install(&host);
}
