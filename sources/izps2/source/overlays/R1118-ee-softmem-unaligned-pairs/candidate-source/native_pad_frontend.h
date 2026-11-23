#ifndef PS2_NATIVE_PAD_FRONTEND_H
#define PS2_NATIVE_PAD_FRONTEND_H
#include "native_pad.h"
#include "ps2_controller_settings.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    Ps2NativePadFrame frame;
    uint64_t polls,reads,switches,disconnects,owner_thread;
    uint32_t initialized,sdk_verified,max_reads_per_poll,last_error;
} Ps2NativePadStatistics;
/* Own original Pad0 input source. Initialize/poll/shutdown on the SAME private
 * HBXL pthread, with original PAD lifecycle/configuration/SIO reads quiescent.
 * Poll requires IOP mode Off. No GS-thread callback or autonomous worker.
 * The caller retains the existing Launcher HID initialization and SDK RX. */
uint32_t ps2_native_pad_initialize_sdk(uintptr_t sdk_rx,size_t sdk_bytes);
uint32_t ps2_native_pad_initialize_callbacks(const Ps2NativePadApi *);
uint32_t ps2_native_pad_initialize_reader(Ps2NativePadReader, const void *);
/* PCSX2 unified slots, in player order. Only the CPU owner can change them. */
uint32_t ps2_native_pad_configure_players(const unsigned *slots,
                                          const uint32_t devices[PS2_PAD_PLAYERS],
                                          const Ps2PadProfile profiles[PS2_PAD_PLAYERS], unsigned count);
uint32_t ps2_native_pad_poll(void);
/* Application CPU caller owns menu gestures until release. Keep fresh host
 * samples/statistics, while all guest buttons/axes stay neutral during polls. */
void ps2_native_pad_set_input_owned(uint32_t);
uint32_t ps2_native_pad_shutdown(void);
uint32_t ps2_native_pad_statistics(Ps2NativePadStatistics *);
uint32_t ps2_native_pad_menu_sample(Ps2NativePadFrame *, unsigned *connected_mask);
#ifdef __cplusplus
}
#endif
#endif
