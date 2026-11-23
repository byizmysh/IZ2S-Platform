#ifndef PS2_SW_TEMPORAL_STORAGE_H
#define PS2_SW_TEMPORAL_STORAGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Ps2SwTemporalStatistics {
    uint64_t allocations, releases, live_bytes, peak_bytes, failures;
} Ps2SwTemporalStatistics;
extern Ps2SwTemporalStatistics ps2_sw_temporal_statistics;
/* One serial MTGS owner. CPU pixel data uses the ordinary cached allocator;
 * it has no SDK thread, TLS or IPC address requirement. */
uint8_t *ps2_sw_temporal_acquire(void);
void ps2_sw_temporal_release(void);
#ifdef __cplusplus
}
#endif
#endif
