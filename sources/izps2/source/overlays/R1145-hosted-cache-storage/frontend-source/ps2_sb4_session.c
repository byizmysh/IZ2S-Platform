#include "ps2_sb4_session.h"

enum { work_units = 16 };

static void boot_stage(IzNativeApp *app, const char *message, uint32_t bytes) {
    if (app->write_file) app->write_file(app, "r1145-frontend-start.log", message, bytes);
}

#define BOOT_STAGE(app, text) boot_stage((app), (text), sizeof(text) - 1)

static uint32_t retain(Ps2Sb4Session *session) {
    session->retained = 1;
    return PS2_SB4_SESSION_RETAINED;
}

static int context_valid(const Ps2Sb4Session *session, const IzNativeApp *app,
                         const Ps2Sb4SessionSetup *setup) {
    uintptr_t module = (uintptr_t)app->data, worker = (uintptr_t)setup->worker;
    uintptr_t owner = (uintptr_t)session;
    if ((worker >= owner && worker - owner < sizeof(*session)) ||
        (owner >= worker && owner - worker < setup->worker_bytes)) return 0;
    return module && worker >= module && worker % 8 == 0 &&
        worker - module <= app->data_bytes && setup->worker_bytes >= sizeof(Ps2Sb4Worker) &&
        setup->worker_bytes <= app->data_bytes - (worker - module);
}

uint32_t ps2_sb4_session_begin(Ps2Sb4Session *session, IzNativeApp *app,
                              const Ps2Sb4SessionSetup *setup) {
    if (!session || session->app || !setup || !setup->engine_asset || !*setup->engine_asset ||
        !sb4ModuleCompat(app) || !app->request_exit || !context_valid(session, app, setup))
        return PS2_SB4_SESSION_INVALID;
    const IzNativeApp6 *native = (const IzNativeApp6 *)app;
    const IzNativeApp5 *worker_api = &native->base;
    const IzNativeApp4 *image_api = &worker_api->base;
    const Sb4Storage *storage = sb4ModuleStorage(app);
    Ps2NativeAudioConfig audio;
    if (!image_api->image_begin || !image_api->image_step || !image_api->image_release ||
        !worker_api->worker_begin || !worker_api->worker_poll || !worker_api->worker_join ||
        !native->graphics || !native->presentation_acquire || !native->presentation_release ||
        !sb4StorageWorkerHas(storage, SB4_STORAGE_WORKER_READ | SB4_STORAGE_WORKER_WRITE) ||
        !ps2_sb4_audio_bind(app, setup->audio_arena, setup->audio_arena_bytes, &audio))
        return PS2_SB4_SESSION_INVALID;

    Ps2Sb4Media media;
    uint32_t result = ps2_sb4_media_prepare(app, setup->bios_filename, setup->disc_filename, &media);
    if (result) return result;
    uint64_t ticket = 0;
    BOOT_STAGE(app, "01 image begin");
    result = image_api->image_begin(app, setup->engine_asset, &ticket);
    if (result && !ticket) return result;
    *session = (Ps2Sb4Session){
        .app = app, .worker = setup->worker, .worker_bytes = setup->worker_bytes,
        .target = setup->video, .image_ticket = ticket,
    };
    if (result || !ticket) return retain(session);
    *setup->worker = (Ps2Sb4Worker){.app = app, .audio = audio, .video = &session->video.video, .media = media};
    return 0;
}

static uint32_t step_image(Ps2Sb4Session *session, IzNativeImageProgress *progress,
                           IzNativeImage *receipt) {
    const IzNativeApp4 *api = (const IzNativeApp4 *)session->app;
    uint32_t result = api->image_step(session->app, session->image_ticket, work_units, progress, receipt);
    if (progress->version == 1 && progress->bytes == sizeof(*progress)) {
        if (progress->state == IZ_NATIVE_IMAGE_LOADING || progress->state == IZ_NATIVE_IMAGE_READY)
            session->image_stage = progress->stage;
        if (progress->error) session->start_error = progress->error;
    }
    if (result && !session->start_error) session->start_error = result;
    if (progress->version != 1 || progress->bytes != sizeof(*progress) ||
        progress->state < IZ_NATIVE_IMAGE_LOADING || progress->state > IZ_NATIVE_IMAGE_RETAINED ||
        progress->state == IZ_NATIVE_IMAGE_RETAINED || progress->done > 1)
        return retain(session);
    if (progress->state == IZ_NATIVE_IMAGE_CLOSED || progress->state == IZ_NATIVE_IMAGE_FAILED) {
        if (!progress->done || receipt->token) return retain(session);
        session->image_ticket = 0;
        return result ? result : progress->error;
    }
    if (result) return retain(session);
    return 0;
}

static uint32_t poll_worker(Ps2Sb4Session *session, IzNativeWorkerProgress *progress) {
    const IzNativeApp5 *api = (const IzNativeApp5 *)session->app;
    uint32_t result = api->worker_poll(session->app, session->worker_ticket, progress);
    if (result || progress->version != 1 || progress->bytes != sizeof(*progress) || progress->reserved ||
        (progress->state != IZ_WORKER_RUNNING && progress->state != IZ_WORKER_FINISHED))
        return retain(session);
    return 0;
}

static uint32_t return_from_worker(Ps2Sb4Session *session) {
    const uint32_t action = session->return_action;
    const uint32_t rc = ps2_sb4_session_stop(session);
    if (rc == IZ_NATIVE_STOP_PENDING) return 0;
    return rc ? rc : action;
}

uint32_t ps2_sb4_session_step(Ps2Sb4Session *session) {
    if (!session || !session->app) return PS2_SB4_SESSION_INVALID;
    if (session->retained) return PS2_SB4_SESSION_RETAINED;
    if (session->return_action) return return_from_worker(session);
    if (session->exit_requested) return 0;
    IzNativeApp *app = session->app;
    const IzNativeApp5 *api = (const IzNativeApp5 *)app;
    if (session->worker_ticket) {
        session->start_stage = PS2_SB4_START_ENGINE;
        IzNativeWorkerProgress progress = {0};
        uint32_t result = poll_worker(session, &progress);
        if (result || progress.state == IZ_WORKER_RUNNING) return result;
        if (progress.result == PS2_SB4_SESSION_CHOOSE_GAME ||
            progress.result == PS2_SB4_SESSION_RESTART_GAME || progress.result == PS2_SB4_SESSION_BOOT_BIOS) {
            session->return_action = (uint32_t)progress.result;
            return return_from_worker(session);
        }
        if (progress.result)
            return progress.result <= UINT32_MAX ? (uint32_t)progress.result : PS2_SB4_SESSION_ENGINE;
        session->exit_requested = 1;
        app->request_exit(app);
        return 0;
    }
    if (!session->image_ticket) return PS2_SB4_SESSION_INVALID;
    if (session->load_percent < 100) {
        session->start_stage = PS2_SB4_START_IMAGE;
        IzNativeImageProgress progress = {0};
        IzNativeImage receipt = {0};
        uint32_t result = step_image(session, &progress, &receipt);
        if (result) return result;
        if (!session->image_ticket) return PS2_SB4_SESSION_ENGINE;
        if (progress.state != IZ_NATIVE_IMAGE_READY) {
            if (progress.total_bytes && progress.read_bytes <= progress.total_bytes) {
                uint64_t read = progress.read_bytes, total = progress.total_bytes;
                if (total > UINT64_MAX / 100) { read /= 100; total /= 100; }
                const uint32_t percent = read == total ? 99 : (uint32_t)(read * 100 / total);
                if (percent > session->load_percent) session->load_percent = percent;
            }
            return 0;
        }
        if (!progress.done || receipt.version != 1 || receipt.bytes != sizeof(receipt) ||
            receipt.token != session->image_ticket || !receipt.entry || !receipt.code || !receipt.code_bytes ||
            !receipt.data || !receipt.data_bytes)
            return retain(session);
        // Give the frontend one frame to publish the loaded state before passing it to the engine.
        BOOT_STAGE(app, "02 image ready");
        session->load_percent = 100;
        return 0;
    }
    session->start_stage = PS2_SB4_START_VIDEO;
    BOOT_STAGE(app, "03 video begin");
    uint32_t result = ps2_sb4_video_begin(&session->video, app, &session->target);
    if (result) return result;
    session->worker->vulkan_profile = &session->video.profile;
    session->start_stage = PS2_SB4_START_WORKER;
    BOOT_STAGE(app, "04 worker begin");
    result = api->worker_begin(app, session->image_ticket, session->worker, session->worker_bytes,
                               &session->worker_ticket);
    if (result || !session->worker_ticket) {
        /* The launcher promises an absent ticket for confirmed rejection.
         * Its retained-worker result can also carry an absent ticket. */
        if (!result || session->worker_ticket || result == IZ_NATIVE_WORKER_RETAINED) {
            session->start_error = result;
            return retain(session);
        }
        return result;
    }
    BOOT_STAGE(app, "05 worker started");
    return 0;
}

uint32_t ps2_sb4_session_stop(Ps2Sb4Session *session) {
    if (!session) return PS2_SB4_SESSION_INVALID;
    if (session->retained) return PS2_SB4_SESSION_RETAINED;
    if (!session->app) return 0;
    IzNativeApp *app = session->app;
    const IzNativeApp5 *api = (const IzNativeApp5 *)app;
    if (session->worker_ticket) {
        __atomic_store_n(&session->worker->stop_requested, 1, __ATOMIC_RELEASE);
        IzNativeWorkerProgress progress = {0};
        uint32_t result = poll_worker(session, &progress);
        if (result) return result;
        if (progress.state == IZ_WORKER_RUNNING) return IZ_NATIVE_STOP_PENDING;
        result = api->worker_join(app, session->worker_ticket, &session->worker_result);
        if (result || session->worker_result != progress.result ||
            !__atomic_load_n(&session->worker->resources_closed, __ATOMIC_ACQUIRE))
            return retain(session);
        session->worker_ticket = 0;
    }
    if (ps2_sb4_video_finish(&session->video)) return retain(session);
    if (session->image_ticket) {
        if (!session->image_closing) {
            if (api->base.image_release(app, session->image_ticket)) return retain(session);
            session->image_closing = 1;
        }
        IzNativeImageProgress progress = {0};
        IzNativeImage receipt = {0};
        step_image(session, &progress, &receipt);
        if (session->retained) return PS2_SB4_SESSION_RETAINED;
        if (session->image_ticket) return IZ_NATIVE_STOP_PENDING;
    }
    *session->worker = (Ps2Sb4Worker){0};
    uint64_t result = session->worker_result;
    *session = (Ps2Sb4Session){.worker_result = result};
    return 0;
}

int ps2_sb4_worker_platform(const Ps2Sb4Worker *worker, Ps2Sb4Platform *platform) {
    return worker && ps2_sb4_platform_attach(worker->app, platform);
}

int ps2_sb4_worker_should_stop(const Ps2Sb4Worker *worker) {
    return !worker || __atomic_load_n(&worker->stop_requested, __ATOMIC_ACQUIRE) != 0;
}

uint32_t ps2_sb4_worker_release(Ps2Sb4Worker *worker) {
    if (!worker || !worker->app) return PS2_SB4_SESSION_INVALID;
    if (worker->platform_retained) return PS2_SB4_SESSION_RETAINED;
    return ps2_sb4_sd_saves_detach(worker->app);
}

uint32_t ps2_sb4_worker_finish(Ps2Sb4Worker *worker) {
    uint32_t result = ps2_sb4_worker_release(worker);
    if (!result) __atomic_store_n(&worker->resources_closed, 1, __ATOMIC_RELEASE);
    return result;
}
