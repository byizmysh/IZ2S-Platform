// Written only by the serial presenter and read after the GS worker joins.
typedef struct {
    uint64_t pairs, total_ticks, max_ticks, max_begin_tick;
    uint64_t slow_pairs, last_slow_begin_tick, last_slow_ticks, rejected;
} NativeVideoQueueStage;
enum { NATIVE_VIDEO_ACQUIRE, NATIVE_VIDEO_SUBMIT, NATIVE_VIDEO_PRESENT, NATIVE_VIDEO_STAGES };
__attribute__((used, visibility("hidden")))
NativeVideoQueueStage ps2_native_video_queue_timing[NATIVE_VIDEO_STAGES];
_Static_assert(sizeof(ps2_native_video_queue_timing) == 192, "post-join queue timing layout");

static uint64_t video_queue_begin(void){
    return presenter_fence_wait_timing.qualified ? hbxl_svc_tick() : 0;
}

static void video_queue_end(unsigned stage, uint64_t begin){
    if(!presenter_fence_wait_timing.qualified)return;
    const uint64_t end=hbxl_svc_tick();
    NativeVideoQueueStage *timing=&ps2_native_video_queue_timing[stage];
    if(end<begin){++timing->rejected;return;}
    const uint64_t elapsed=end-begin;
    ++timing->pairs;
    timing->total_ticks=elapsed>UINT64_MAX-timing->total_ticks ? UINT64_MAX : timing->total_ticks+elapsed;
    if(elapsed>timing->max_ticks){
        timing->max_ticks=elapsed;
        timing->max_begin_tick=begin;
    }
    // Track the most recent call spanning at least one 60 Hz frame interval.
    const uint64_t hz=presenter_fence_wait_timing.tick_hz;
    if(elapsed>=(hz+59)/60){
        ++timing->slow_pairs;
        timing->last_slow_begin_tick=begin;
        timing->last_slow_ticks=elapsed;
    }
}
