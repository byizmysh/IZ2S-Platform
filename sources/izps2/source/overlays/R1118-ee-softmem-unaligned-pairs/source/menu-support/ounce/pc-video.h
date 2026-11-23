#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IZ_PC_VIDEO_CAPACITY (0x100000u + 16u)
#define IZ_PC_PCM_BYTES 3840u
#define IZ_PC_MEDIA_CAPACITY (0x100000u + 28u + IZ_PC_PCM_BYTES)
#define IZ_PC_INPUT_BYTES 396u

enum IzPcVideoResult { IZ_PC_VIDEO_ERROR, IZ_PC_VIDEO_EMPTY, IZ_PC_VIDEO_CURRENT, IZ_PC_VIDEO_FRAME };
typedef struct {
    uint32_t sequence, width, height, format;
    const uint8_t *pixels;
    size_t bytes;
} IzPcVideoFrame;

typedef struct {
    IzPcVideoFrame video;
    uint32_t audio_sequence;
    const uint8_t *pcm;
} IzPcMediaFrame;

typedef struct {
    void *context;
    bool (*transfer)(void*,void*,size_t,bool sending);
    void (*sign)(void*,uint8_t[32],const void*,size_t);
    void (*hash)(void*,const void*,size_t);
} IzPcVideoIo;

/* On FRAME, pixels refers to the caller's authenticated packet buffer. */
enum IzPcVideoResult iz_pc_video_receive(const IzPcVideoIo *,uint8_t operation,uint32_t seen,
                                        uint8_t *body,size_t capacity,IzPcVideoFrame *frame);
enum IzPcVideoResult iz_pc_media_receive(const IzPcVideoIo *,uint8_t operation,uint32_t seen,uint32_t heard,
                                        const uint8_t input[IZ_PC_INPUT_BYTES],uint8_t *body,size_t capacity,IzPcMediaFrame *frame);
struct IzNativeApp;
void iz_pc_capture_controls(const struct IzNativeApp *,uint8_t packet[IZ_PC_INPUT_BYTES]);
