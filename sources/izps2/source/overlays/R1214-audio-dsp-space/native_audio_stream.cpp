#include "native_audio_stream.h"
#include "Host/AudioStream.h"
#include "common/Error.h"
#include "common/Timer.h"
#include "stereo-dsp-block-queue.h"
#include <atomic>
#include <cstdio>
#include <type_traits>
#ifdef IZPS2_SDK_DSP_WAKE
#include "audio-dsp-wake-sdk.h"
#endif

struct IzNativeApp;
extern "C" void izps2_boot_stage(IzNativeApp*, const char*, uint32_t);
#define AUDIO_EXIT_STAGE(message) izps2_boot_stage(nullptr, "26 audio " message "\n", sizeof("26 audio " message "\n") - 1)

namespace {
class NativeAudioStream;
std::atomic<NativeAudioStream*> active{nullptr};
Ps2LiveAudioHost binding{}; // immutable while a stream exists; install before VM
bool installed=false;
bool binding_retained=false;
bool startup_failed=false;
bool stretch_worker_trial=false;
using ProcessStretch = void (*)(AudioStream*, const float*);
enum Phase {Starting,Running,Stopped,Retained};
class NativeAudioStream final : public AudioStream {
public:
    explicit NativeAudioStream(unsigned rate,const AudioStreamParameters&p,const Ps2LiveAudioHost&h,bool stretch)
        :AudioStream(rate,p),host(h),async_stretch(stretch_worker_trial) {
        static_assert(std::is_same_v<SampleType,float>);
#ifdef IZPS2_SDK_DSP_WAKE
        if (async_stretch) {
            dsp_event.reset(iz_audio_dsp_wake_create());
            if (dsp_event)
                dsp_space_event.reset(iz_audio_dsp_wake_create());
        }
#endif
        BaseInitialize(&StereoSampleReaderImpl,stretch);
        AudioStream::SetPaused(true); // controls configured before the first pull
    }
    // This method and the control calls are owned by the emulation thread.
    bool submit_stretch(const float* samples, ProcessStretch process) {
        if (!async_stretch || IsPaused() || done.load(std::memory_order_acquire))
            return false;
        if (queue_stretch(samples, process))
            return true;
        ++dsp_full;
#ifdef IZPS2_SDK_DSP_WAKE
        if (queue_stretch_after_reclaim(samples, process))
            return true;
#endif
        // The acknowledgement still guards the synchronous fallback when
        // the worker cannot reclaim space within one SDK wait.
        ++dsp_full_handoffs;
        SetPaused(true);
        process(this, samples);
        SetPaused(false);
        return true;
    }
    ~NativeAudioStream() override {
        if (!shutdown()) {
#ifdef IZPS2_WRAPPER_AUDIT
            std::fprintf(stderr, "IZPS2_AUDIO_TIMING controls=%llu control_ms=%.3f appends=%llu retries=%llu\n",
                static_cast<unsigned long long>(control_calls), Common::Timer::ConvertValueToMilliseconds(control_ticks),
                static_cast<unsigned long long>(output.appends), static_cast<unsigned long long>(retries));
#endif
            return;
        }
        // SB4 keeps the queued descriptors in the separately owned arena.
        // Once the pull thread is joined, retain that arena and the session.
        if (host.audio.service_abi == PS2_AUDIO_SB4_ABI && joined) {
            binding_retained = true;
            if (host.retained) host.retained(host.context, last.load());
            clear_active();
            return;
        }
        park();
    }
    bool start() {
        if (async_stretch) {
            if (dsp_event)
                AUDIO_EXIT_STAGE("DSP worker uses SDK wake");
            else
                AUDIO_EXIT_STAGE("DSP worker uses 1 ms polling");
        }
        const int r=host.start_worker(host.context,&entry,this,&thread);
        if(r==1){phase.store(Stopped);last.store(PS2_LIVE_AUDIO_INVALID);return false;}
        if(r){last.store(PS2_LIVE_AUDIO_THREAD_UNKNOWN);phase.store(Retained);park();}
        created=true;
        for(unsigned ms=0;phase.load(std::memory_order_acquire)==Starting;ms++){
            if(ms==host.control_wait_ms){last.store(PS2_LIVE_AUDIO_CONTROL_TIMEOUT);return false;}
            sleep();
        }
        return phase.load(std::memory_order_acquire)==Running;
    }
    void SetPaused(bool pause) override {
        if(done.load(std::memory_order_acquire)){AudioStream::SetPaused(pause);return;}
#ifdef IZPS2_WRAPPER_AUDIT
        const auto start = Common::Timer::GetCurrentValue();
#endif
        // Sequence token prevents a stale acknowledgement from a previous pause.
        const uint64_t token=((command.load(std::memory_order_relaxed)>>1)+1)*2+(pause?1:0);
        if(!pause)AudioStream::SetPaused(false);
        command.store(token,std::memory_order_release);
        for(unsigned ms=0;acknowledged.load(std::memory_order_acquire)!=token;ms++){
            if(done.load(std::memory_order_acquire))break;
            if(ms==host.control_wait_ms){last.store(PS2_LIVE_AUDIO_CONTROL_TIMEOUT);park();}
            sleep();
        }
        if(pause)AudioStream::SetPaused(true);
#ifdef IZPS2_WRAPPER_AUDIT
        ++control_calls;
        control_ticks += Common::Timer::GetCurrentValue() - start;
#endif
    }
    template<typename F> void control(F change){
        const bool paused=IsPaused();SetPaused(true);change();if(!paused)SetPaused(false);
    }
    template<typename F> void empty_buffer(F change){
        const bool paused=IsPaused();SetPaused(true);
        // Pausing the ring alone leaves a pulled suffix and borrowed AudioOut
        // buffers alive. The worker retires those buffers before the caller
        // changes the guest timeline; only it may access the native owner.
        if(!done.load(std::memory_order_acquire)){
            const uint64_t token=reset_requested.load(std::memory_order_relaxed)+1;
            reset_requested.store(token,std::memory_order_release);
            const unsigned limit=host.control_wait_ms+host.drain_wait_ms;
            for(unsigned ms=0;reset_acknowledged.load(std::memory_order_acquire)!=token;ms++){
                if(done.load(std::memory_order_acquire)||ms==limit){
                    if(!last.load())last.store(PS2_LIVE_AUDIO_CONTROL_TIMEOUT);
                    park();
                }
                sleep();
            }
        }else if(phase.load(std::memory_order_acquire)!=Stopped)park();
        change();if(!paused)SetPaused(false);
    }
#ifdef PS2_LIVE_AUDIO_VIRTUAL_RESET
    void EmptyBuffer() override {
        empty_buffer([this] { AudioStream::EmptyBuffer(); });
    }
#endif
    int shutdown(){
        if(!created){clear_active();return phase.load()==Stopped?0:PS2_LIVE_AUDIO_RETAINED;}
        AUDIO_EXIT_STAGE("request worker stop");
        stop.store(true,std::memory_order_release);
        const unsigned limit=host.control_wait_ms+host.drain_wait_ms*2;
        for(unsigned ms=0;!done.load(std::memory_order_acquire);ms++){
            if(ms==limit){
                switch (shutdown_stage.load(std::memory_order_acquire)) {
                    case ShutdownStage::Running: AUDIO_EXIT_STAGE("timeout before drain"); break;
                    case ShutdownStage::Draining: AUDIO_EXIT_STAGE("timeout draining stream"); break;
                    case ShutdownStage::Closing: AUDIO_EXIT_STAGE("timeout in native close"); break;
                }
                last.store(PS2_LIVE_AUDIO_CONTROL_TIMEOUT);
                phase.store(Retained);
                return PS2_LIVE_AUDIO_CONTROL_TIMEOUT;
            }
            sleep();
        }
        AUDIO_EXIT_STAGE("worker published done");
        if(!joined){
            AUDIO_EXIT_STAGE("join begin");
            if(host.join_worker(host.context,thread)){
                last.store(PS2_LIVE_AUDIO_JOIN_UNKNOWN);phase.store(Retained);return PS2_LIVE_AUDIO_JOIN_UNKNOWN;
            }
            joined=true;
            AUDIO_EXIT_STAGE("join returned");
        }
        char outcome[200];
        const int size = std::snprintf(outcome, sizeof(outcome),
            "26 audio result=%d sdk=0x%08x phase=%d open=%u pending=%u uncertain=%u staged=%u empty_queue_polls=%llu\n",
            last.load(), output.sdk_result, phase.load(), output.opened, output.pending,
            output.uncertain, stage_count, static_cast<unsigned long long>(empty_queue_polls));
        if (size > 0 && static_cast<size_t>(size) < sizeof(outcome))
            izps2_boot_stage(nullptr, outcome, static_cast<uint32_t>(size));
        if(phase.load()!=Stopped||output.opened||output.pending||output.uncertain||stage_count)
            return PS2_LIVE_AUDIO_RETAINED;
        if (!queue_reported) {
            char queue[256];
            const int length = std::snprintf(queue, sizeof(queue),
                "26 audio queue gaps=%llu gap_ms=%.3f max_ms=%.3f submitted=%llu completed=%llu "
                "dsp_blocks=%llu dsp_full=%llu wake_waits=%llu wakes=%llu\n",
                static_cast<unsigned long long>(gap_count),
                Common::Timer::ConvertValueToMilliseconds(gap_total),
                Common::Timer::ConvertValueToMilliseconds(gap_max),
                static_cast<unsigned long long>(output.submitted_frames),
                static_cast<unsigned long long>(output.completed_frames),
                dsp_blocks, dsp_full, dsp_event_waits, dsp_event_signals);
            if (length > 0 && static_cast<size_t>(length) < sizeof(queue))
                izps2_boot_stage(nullptr, queue, static_cast<uint32_t>(length));
#ifdef IZPS2_SDK_DSP_WAKE
            char pressure[200];
            const int pressure_length = std::snprintf(pressure, sizeof(pressure),
                "26 audio dsp pressure waits=%llu recovered=%llu control_handoffs=%llu\n",
                dsp_space_waits, dsp_space_recovered, dsp_full_handoffs);
            if (pressure_length > 0 && static_cast<size_t>(pressure_length) < sizeof(pressure))
                izps2_boot_stage(nullptr, pressure, static_cast<uint32_t>(pressure_length));
#endif
            queue_reported = true;
        }
        clear_active();return 0;
    }
    void status(Ps2LiveAudioStatus&s){
        s={};s.active=phase.load(std::memory_order_acquire)==Running&&!stop.load();s.worker_created=created;
        s.worker_done=done.load(std::memory_order_acquire);s.worker_joined=joined;
        s.retained=phase.load()==Retained;s.last_error=last.load();
        // Output/staging counters have one writer. Acquire done before reading.
        if(s.worker_done){s.closed=!output.opened&&!output.uncertain&&!output.pending;
            s.pulls=pulls;s.pulled_frames=pulled;s.staging_retries=retries;s.pause_acknowledgements=pauses;
            s.accepted_frames=output.accepted_frames;s.submitted_frames=output.submitted_frames;
            s.completed_frames=output.completed_frames;s.appends=output.appends;s.releases=output.releases;
            s.pending=output.pending;s.uncertain=output.uncertain;
        }
    }
    [[noreturn]] void park(){
        phase.store(Retained,std::memory_order_release);
        if(host.retained)host.retained(host.context,last.load());
        for(;;)sleep(); // this, base ring, arena, SDK callbacks and code stay resident
    }
private:
    void clear_active(){NativeAudioStream*me=this;active.compare_exchange_strong(me,nullptr);}
    void sleep(){host.sleep_ns(1000000);}
    bool queue_stretch(const float* samples, ProcessStretch process) {
        if (!dsp_queue.try_push(samples, process))
            return false;
#ifdef IZPS2_SDK_DSP_WAKE
        if (dsp_event)
            iz_audio_dsp_wake_signal(dsp_event.get());
#endif
        return true;
    }
#ifdef IZPS2_SDK_DSP_WAKE
    bool queue_stretch_after_reclaim(const float* samples, ProcessStretch process) {
        if (!dsp_space_event)
            return false;

        // Arm before rechecking the queue to cover a completion between
        // the failed enqueue and arming. A wake is a hint, not a slot.
        auto* const wake = dsp_space_event.get();
        iz_audio_dsp_wake_arm(wake);
        bool queued = queue_stretch(samples, process);
        if (!queued) {
            ++dsp_space_waits;
            iz_audio_dsp_wake_wait(wake);
            queued = queue_stretch(samples, process);
        }
        iz_audio_dsp_wake_disarm(wake);
        dsp_space_recovered += queued;
        return queued;
    }
#endif
    void wait_idle_dsp() {
#ifdef IZPS2_SDK_DSP_WAKE
        // Keep the existing polling deadline for staged PCM. The event only
        // shortens the wait when no partially filled AudioOut slot is due.
        if (dsp_event && output.staged_frames == 0) {
            iz_audio_dsp_wake_arm(dsp_event.get());
            if (dsp_queue.empty()) {
                ++dsp_event_waits;
                dsp_event_signals += iz_audio_dsp_wake_wait(dsp_event.get());
            }
            iz_audio_dsp_wake_disarm(dsp_event.get());
            return;
        }
#endif
        sleep();
    }
    // Only the AudioOut worker measures intervals between an observed empty
    // running queue and its next successful submission. Pauses end the interval.
    void finish_gap() {
        if (!gap_start) return;
        const auto elapsed = Common::Timer::GetCurrentValue() - gap_start;
        gap_total += elapsed;
        if (elapsed > gap_max) gap_max = elapsed;
        gap_start = 0;
    }
    void observe_empty_queue() {
        ++empty_queue_polls;
        if (!gap_start) {
            gap_start = Common::Timer::GetCurrentValue();
            ++gap_count;
        }
    }
    static void*entry(void*p){static_cast<NativeAudioStream*>(p)->run();return nullptr;}
    bool retryable(int r){return !r||r==PS2_AUDIO_BACKPRESSURE||r==PS2_AUDIO_REJECTED;}
    int poll_pending() {
        if (output.opened && output.format_verified && !output.uncertain && output.pending == 0)
            return 0;
        return ps2_native_audio_poll(&output);
    }
    int feed_stage(){
        size_t used=0;
        const int r=ps2_native_audio_feed(&output,staging+stage_offset*2,stage_count-stage_offset,&used);
        stage_offset+=static_cast<unsigned>(used);
        if(stage_offset==stage_count)stage_offset=stage_count=0;
        native_retry=(r==PS2_AUDIO_REJECTED);
        if(r==PS2_AUDIO_BACKPRESSURE||r==PS2_AUDIO_REJECTED)retries++;
        return r;
    }
    int reset_output(){
        // Complete only PCM already accepted by the native provider. Do not
        // pull from the old ring, or submit its unconsumed float suffix.
        unsigned waited=0;
        for(;;){
            const int r=ps2_native_audio_drain(&output,host.drain_wait_ms-waited);
            if(r!=PS2_AUDIO_REJECTED&&r!=PS2_AUDIO_BACKPRESSURE){
                if(r)return r;
                stage_count=stage_offset=0;native_retry=false;return 0;
            }
            if(waited==host.drain_wait_ms)return PS2_AUDIO_TIMEOUT;
            retries++;sleep();waited++;
        }
    }
    void drain_stretch() {
        dsp_blocks += dsp_queue.drain([this](const float* samples, ProcessStretch process) {
            process(this, samples);
        }, [this] {
#ifdef IZPS2_SDK_DSP_WAKE
            if (dsp_space_event)
                iz_audio_dsp_wake_signal(dsp_space_event.get());
#endif
        });
    }
    void run(){
        int result=ps2_native_audio_open(&output,&host.audio);
        if(result){
            last.store(result);
            if(output.opened&&!output.uncertain)result=ps2_native_audio_close(&output,host.drain_wait_ms);
            phase.store((output.opened||output.uncertain)?Retained:Stopped,std::memory_order_release);
            done.store(true,std::memory_order_release);return;
        }
        phase.store(Running,std::memory_order_release);
        unsigned idle=0;
        while(!stop.load(std::memory_order_acquire)){
            const uint64_t token=command.load(std::memory_order_acquire);
            if(token&1){
                finish_gap();
                if (acknowledged.load(std::memory_order_relaxed) != token) {
                    ++pauses;
                    if (async_stretch)
                        drain_stretch();
                }
                acknowledged.store(token,std::memory_order_release);
                const uint64_t reset=reset_requested.load(std::memory_order_acquire);
                if(reset_acknowledged.load(std::memory_order_relaxed)!=reset){
                    result=reset_output();if(result)break;
                    reset_acknowledged.store(reset,std::memory_order_release);
                }
                result = poll_pending();
                if (result)
                    break;
                sleep();continue;
            }
            acknowledged.store(token,std::memory_order_release);
            if (async_stretch)
                drain_stretch();
            if(native_retry){
                result=ps2_native_audio_flush_partial(&output);
                native_retry=(result==PS2_AUDIO_REJECTED);
                if (output.pending) finish_gap();
                if(!retryable(result))break;
                if(result){retries++;sleep();continue;}
            }
            if(!stage_count){
                stage_count=PullFrames(staging,host.pull_frames);stage_offset=0;
                if(stage_count){pulls++;pulled+=stage_count;idle=0;}
            }
            if(stage_count){
                result=feed_stage();
                if (output.pending) finish_gap();
                if(!retryable(result))break;
                if(result)sleep();
            }
            else {
                // Partial flush already validates completions. Keep its deadline
                // and use the empty-owner shortcut only on maintenance polls.
                if (++idle >= 4 && output.staged_frames) {
                    result = ps2_native_audio_flush_partial(&output);
                    native_retry = (result == PS2_AUDIO_REJECTED);
                    if (output.pending) finish_gap();
                    if (!retryable(result))
                        break;
                } else {
                    result = poll_pending();
                    if (result)
                        break;
                    if (output.started && !output.pending)
                        observe_empty_queue();
                }
                wait_idle_dsp();
            }
        }
        finish_gap();
        shutdown_stage.store(ShutdownStage::Draining, std::memory_order_release);
        if(retryable(result)){
            if (async_stretch)
                drain_stretch();
            unsigned waited=0;
            for(;;){
                if(!stage_count&&!native_retry){
                    stage_count=PullFrames(staging,host.pull_frames);stage_offset=0;
                    if(stage_count){pulls++;pulled+=stage_count;}else break;
                }
                if(native_retry){result=ps2_native_audio_flush_partial(&output);native_retry=(result==PS2_AUDIO_REJECTED);}
                else result=feed_stage();
                if(!retryable(result))break;
                if(result){if(waited==host.drain_wait_ms){result=PS2_AUDIO_TIMEOUT;break;}sleep();waited++;}
            }
            if(!stage_count&&!native_retry&&retryable(result)) {
                shutdown_stage.store(ShutdownStage::Closing, std::memory_order_release);
                result=ps2_native_audio_close(&output,host.drain_wait_ms);
            }
        }
        last.store(result);
        phase.store(!result&&!output.opened&&!output.uncertain&&!output.pending&&!stage_count?Stopped:Retained,std::memory_order_release);
        done.store(true,std::memory_order_release);
    }
    enum class ShutdownStage { Running, Draining, Closing };
    std::atomic<ShutdownStage> shutdown_stage{ShutdownStage::Running};
    Ps2LiveAudioHost host;
    using DspQueue = StereoDspBlockQueue<CHUNK_SIZE, ProcessStretch>;
    const bool async_stretch;
    bool queue_reported = false;
    unsigned long long dsp_blocks = 0, dsp_full = 0;
    unsigned long long dsp_full_handoffs = 0;
    DspQueue dsp_queue;
#ifdef IZPS2_SDK_DSP_WAKE
    struct DspWakeDeleter {
        void operator()(IzAudioDspWake* wake) const { iz_audio_dsp_wake_destroy(wake); }
    };
    // The stream either joins the worker before members are freed or parks.
    std::unique_ptr<IzAudioDspWake, DspWakeDeleter> dsp_event, dsp_space_event;
    unsigned long long dsp_event_waits = 0, dsp_event_signals = 0;
    unsigned long long dsp_space_waits = 0, dsp_space_recovered = 0;
#endif
    Ps2NativeAudio output{};
    float staging[PS2_AUDIO_SLOT_FRAMES*2]{};
    uint64_t empty_queue_polls = 0, gap_count = 0;
    Common::Timer::Value gap_start = 0, gap_total = 0, gap_max = 0;
    unsigned stage_count=0,stage_offset=0;
    bool created=false,joined=false,native_retry=false;
    uint64_t thread=0,pulls=0,pulled=0,retries=0,pauses=0;
#ifdef IZPS2_WRAPPER_AUDIT
    uint64_t control_calls = 0;
    Common::Timer::Value control_ticks = 0;
#endif
    std::atomic<uint64_t>command{1},acknowledged{0};
    std::atomic<uint64_t>reset_requested{0},reset_acknowledged{0};
    std::atomic<bool>stop{false},done{false};
    std::atomic<int>phase{Starting},last{0};
};
NativeAudioStream*native(AudioStream*s){auto*p=active.load(std::memory_order_acquire);return p==s?p:nullptr;}
}
extern "C" void ps2_live_audio_set_stretch_worker(bool enabled) {
    stretch_worker_trial = enabled;
    std::fprintf(stderr, "IZPS2_AUDIO_DSP_WORKER enabled=%u\n", unsigned(enabled));
}
extern "C" bool ps2_live_audio_submit_stretch(AudioStream* stream, const float* samples, ProcessStretch process) {
    if (auto* owner = native(stream))
        return owner->submit_stretch(samples, process);
    return false;
}

int ps2_live_audio_install(const Ps2LiveAudioHost*h){
    if(!h||active.load()||binding_retained||!h->start_worker||!h->join_worker||!h->sleep_ns||!h->audio.arena||
       h->audio.startup_slots!=1||!h->pull_frames||h->pull_frames>PS2_AUDIO_SLOT_FRAMES||
       !h->control_wait_ms||h->control_wait_ms>10000||h->drain_wait_ms>10000)return PS2_LIVE_AUDIO_INVALID;
    binding=*h;installed=true;startup_failed=false;return 0;
}
bool ps2_live_audio_installed() { return installed; }
bool ps2_live_audio_startup_failed() { return startup_failed; }
int ps2_live_audio_uninstall() {
    if (active.load(std::memory_order_acquire) || binding_retained) return PS2_LIVE_AUDIO_RETAINED;
    binding = {};
    installed = false;
    startup_failed = false;
    return 0;
}
std::unique_ptr<AudioStream> ps2_live_audio_create(unsigned rate,const AudioStreamParameters&p,bool stretch,Error*e){
    if(!installed||active.load()||binding_retained||rate!=48000||p.expansion_mode!=AudioExpansionMode::Disabled||!p.buffer_ms){
        if (installed) startup_failed = true;
        Error::SetStringView(e,"Native audio requires one installed owner, stereo 48000 Hz and a nonzero buffer.");return nullptr;
    }
    auto stream=std::make_unique<NativeAudioStream>(rate,p,binding,stretch);active.store(stream.get(),std::memory_order_release);
    if(!stream->start()){
        startup_failed = true;
        Error::SetStringView(e,"Native audio startup failed; no working audio is qualified.");
        return nullptr;
    }
    return stream;
}
int ps2_live_audio_shutdown(AudioStream*s){auto*p=native(s);return p?p->shutdown():PS2_LIVE_AUDIO_INVALID;}
bool ps2_live_audio_status(AudioStream*s,Ps2LiveAudioStatus*out){
    // Shutdown releases the arena lease, but the caller still owns its object.
    if(!s||!out)return false;static_cast<NativeAudioStream*>(s)->status(*out);return true;
}
extern "C" std::unique_ptr<AudioStream> __wrap__ZN11AudioStream12CreateStreamE12AudioBackendjRK21AudioStreamParametersPKcS5_bP5Error(
    AudioBackend,unsigned rate,const AudioStreamParameters&p,const char*,const char*,bool stretch,Error*e){
    return ps2_live_audio_create(rate,p,stretch,e);
}
extern "C" void __real__ZN11AudioStream15SetOutputVolumeEj(AudioStream*,unsigned);
extern "C" void __wrap__ZN11AudioStream15SetOutputVolumeEj(AudioStream*s,unsigned v){
    if(auto*p=native(s))p->control([&]{__real__ZN11AudioStream15SetOutputVolumeEj(s,v);});
    else __real__ZN11AudioStream15SetOutputVolumeEj(s,v);
}
extern "C" void __real__ZN11AudioStream14SetNominalRateEf(AudioStream*,float);
extern "C" void __wrap__ZN11AudioStream14SetNominalRateEf(AudioStream*s,float v){
    if(auto*p=native(s))p->control([&]{__real__ZN11AudioStream14SetNominalRateEf(s,v);});
    else __real__ZN11AudioStream14SetNominalRateEf(s,v);
}
#ifndef PS2_LIVE_AUDIO_VIRTUAL_RESET
extern "C" void __real__ZN11AudioStream11EmptyBufferEv(AudioStream*);
extern "C" void __wrap__ZN11AudioStream11EmptyBufferEv(AudioStream*s){
    if(auto*p=native(s))p->empty_buffer([&]{__real__ZN11AudioStream11EmptyBufferEv(s);});
    else __real__ZN11AudioStream11EmptyBufferEv(s);
}
#endif
