#ifndef PS2_SB4_SESSION_H
#define PS2_SB4_SESSION_H

#include "ps2_sb4_audio.h"
#include "ps2_sb4_library.h"
#include "ps2_sb4_platform.h"
#include "ps2_sb4_video.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prefix of the engine's module-owned worker context. The caller may append
 * the selected BIOS/disc and other engine configuration after this prefix. */
typedef struct Ps2Sb4Worker {
    IzNativeApp *app;
    Ps2NativeAudioConfig audio;
    Ps2NativeVideo *video;
    uint32_t stop_requested, resources_closed;
    Ps2Sb4Media media;
    Ps2Sb4Pad pad;
    uint32_t platform_retained;
    const Sb4VulkanDeviceProfile *vulkan_profile;
} Ps2Sb4Worker;

typedef struct {
    const char *engine_asset;
    const char *bios_filename, *disc_filename;
    Ps2Sb4VideoTarget video;
    void *audio_arena;
    size_t audio_arena_bytes;
    Ps2Sb4Worker *worker;
    size_t worker_bytes;
} Ps2Sb4SessionSetup;

typedef struct {
    IzNativeApp *app;
    Ps2Sb4Worker *worker;
    size_t worker_bytes;
    Ps2Sb4VideoTarget target;
    Ps2Sb4Video video;
    uint64_t image_ticket, worker_ticket, worker_result;
    uint32_t image_closing, retained, exit_requested, load_percent;
    uint32_t return_action;
} Ps2Sb4Session;

enum { PS2_SB4_SESSION_INVALID = 0xe8e2, PS2_SB4_SESSION_RETAINED = 0xe8e3,
       PS2_SB4_SESSION_ENGINE = 0xe8e4,
       PS2_SB4_SESSION_CHOOSE_GAME = 0xe8ec, PS2_SB4_SESSION_RESTART_GAME = 0xe8ed, PS2_SB4_SESSION_BOOT_BIOS = 0xe8ee };

/* Stable, zeroed storage. Call begin, step and stop from INIT, FRAME and STOP
 * respectively. All engine and presentation operations use the launcher API. */
uint32_t ps2_sb4_session_begin(Ps2Sb4Session *, IzNativeApp *, const Ps2Sb4SessionSetup *);
uint32_t ps2_sb4_session_step(Ps2Sb4Session *);
uint32_t ps2_sb4_session_stop(Ps2Sb4Session *);

/* The engine worker binds its own file and input state. Publish completion
 * only after its audio, media files and engine-owned threads have closed. */
int ps2_sb4_worker_platform(const Ps2Sb4Worker *, Ps2Sb4Platform *);
int ps2_sb4_worker_should_stop(const Ps2Sb4Worker *);
uint32_t ps2_sb4_worker_release(Ps2Sb4Worker *);
uint32_t ps2_sb4_worker_finish(Ps2Sb4Worker *);

typedef struct {
    uint32_t (*initialize)(IzNativeApp *app);
    uint32_t (*quiesce)(void);
    uint32_t (*finalize)(void);
    int (*can_read_storage_from_threads)(void);
} Ps2Sb4EngineRuntime;

uint64_t ps2_sb4_engine_entry(void *context, uint64_t reserved);
uint64_t ps2_sb4_engine_entry_with_runtime(void *context, uint64_t reserved, const Ps2Sb4EngineRuntime *);
uint64_t ps2_sb4_hosted_entry(void *context, uint64_t reserved);

#ifdef __cplusplus
}
#endif
#endif
