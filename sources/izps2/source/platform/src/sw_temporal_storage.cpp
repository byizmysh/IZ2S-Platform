#include "sw_temporal_storage.h"
#include "sw_frame_mode.h"
#include <cstdlib>

extern "C" { Ps2SwTemporalStatistics ps2_sw_temporal_statistics{}; }
namespace {
uint8_t *pixels;
struct Cleanup {
    ~Cleanup() { ps2_sw_temporal_release(); }
} cleanup;
}

extern "C" uint8_t *ps2_sw_temporal_acquire()
{
    if(pixels)return pixels;
    void *allocated=nullptr;
    if(posix_memalign(&allocated,32,PS2_SW_FRAME_MODE_TEMPORAL_BYTES)) {
        ps2_sw_temporal_statistics.failures++;
        return nullptr;
    }
    pixels=static_cast<uint8_t*>(allocated);
    ps2_sw_temporal_statistics.allocations++;
    ps2_sw_temporal_statistics.live_bytes=PS2_SW_FRAME_MODE_TEMPORAL_BYTES;
    ps2_sw_temporal_statistics.peak_bytes=PS2_SW_FRAME_MODE_TEMPORAL_BYTES;
    return pixels;
}

extern "C" void ps2_sw_temporal_release()
{
    if(!pixels)return;
    std::free(pixels);
    pixels=nullptr;
    ps2_sw_temporal_statistics.releases++;
    ps2_sw_temporal_statistics.live_bytes=0;
}
