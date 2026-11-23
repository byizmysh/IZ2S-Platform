#include <sb4/native-app.h>
#include <sb4/stream.h>
#include <string.h>

/* These words match the original private-video GLSL mailbox. */
enum {
    HeaderOffset = 0x700c80,
    SlotBytes = 0x100000,
    Published = 0, Width = 1, Height = 2, Latched = 3,
    Format = 6, DeltaTransport = 19, SourceReady = 41,
};

static uint32_t *mailbox(const IzNativeApp *app) {
    if (!sb4ModuleCompat(app) || !app->controls || (uintptr_t)app->controls % sizeof(uint32_t) ||
        app->controls_bytes < HeaderOffset + (SourceReady + 1) * sizeof(uint32_t)) return NULL;
    const IzNativeApp2 *video = (const IzNativeApp2 *)app;
    if (!video->private_memory || video->private_bytes < 2 * SlotBytes) return NULL;
    return (uint32_t *)((unsigned char *)app->controls + HeaderOffset);
}

size_t sb4StreamFrameBytes(uint32_t width, uint32_t height, uint32_t format) {
    if (format == SB4_STREAM_RGB565) {
        return width >= 2 && width <= 640 && height >= 2 && height <= 360 && !(width & 1)
            ? width * height * 2 : 0;
    }
    if (width < 4 || height < 4 || (width & 3) || (height & 3)) return 0;
    if (format == SB4_STREAM_BC1 && width <= 1920 && height <= 1080) return width * height / 2;
    if (format == SB4_STREAM_INDEXED8 && width <= 1280 && height <= 720) {
        const size_t bytes = 1024 + width * height;
        return bytes <= 0x78000 ? bytes : 0;
    }
    return 0;
}

uint32_t sb4StreamSubmitFrame(IzNativeApp *app, uint32_t width, uint32_t height,
                             uint32_t format, const void *payload, size_t bytes, uint32_t *publication) {
    if (!publication) return SB4_STREAM_INVALID;
    *publication = 0;
    uint32_t *header = mailbox(app);
    const size_t expected = sb4StreamFrameBytes(width, height, format);
    if (!header || header[DeltaTransport] || !payload || !expected || bytes != expected)
        return SB4_STREAM_INVALID;
    const uint32_t latched = header[Latched];
    if (header[Published] != latched) return SB4_STREAM_PENDING;

    const uint32_t slot = (latched & 1u) ^ 1u;
    uint32_t next = (((latched >> 1) + 1) << 1) | slot;
    if (!next) next = 2;
    const IzNativeApp2 *video = (const IzNativeApp2 *)app;
    memmove((unsigned char *)video->private_memory + slot * SlotBytes, payload, bytes);
    header[Width] = width;
    header[Height] = height;
    header[Format] = format;
    header[Published] = next;
    *publication = next;
    return 0;
}

uint32_t sb4StreamFrameAck(const IzNativeApp *app, uint32_t *publication) {
    if (!publication) return SB4_STREAM_INVALID;
    *publication = 0;
    const uint32_t *header = mailbox(app);
    if (!header || header[DeltaTransport]) return SB4_STREAM_INVALID;
    *publication = header[Latched];
    return 0;
}

uint32_t sb4StreamSetSourceReady(IzNativeApp *app, uint32_t ready) {
    uint32_t *header = mailbox(app);
    if (!header || header[DeltaTransport] || ready > 1) return SB4_STREAM_INVALID;
    header[SourceReady] = ready;
    return 0;
}
