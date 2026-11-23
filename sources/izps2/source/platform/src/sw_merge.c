#include "sw_merge.h"

#include <limits.h>

typedef struct StorageSpan {
    uintptr_t begin, end;
} StorageSpan;

static int image_span(const void *pixels, size_t bytes, size_t pitch,
                      uint32_t width, uint32_t height, StorageSpan *span)
{
    size_t row_bytes, needed;
    uintptr_t address;
    if (!width || !height || width > INT32_MAX || height > INT32_MAX)
        return PS2_SW_MERGE_INVALID;
    if ((uint64_t)width * 4u > SIZE_MAX)
        return PS2_SW_MERGE_STORAGE;
    row_bytes = (size_t)width * 4u;
    if (!pixels || pitch < row_bytes ||
        (size_t)(height - 1u) > (SIZE_MAX - row_bytes) / pitch)
        return PS2_SW_MERGE_STORAGE;
    needed = (size_t)(height - 1u) * pitch + row_bytes;
    address = (uintptr_t)pixels;
    if (bytes < needed || needed > UINTPTR_MAX - address)
        return PS2_SW_MERGE_STORAGE;
    span->begin = address;
    span->end = address + needed;
    return PS2_SW_MERGE_OK;
}

static int valid_rect(Ps2SwMergeRect r)
{
    return r.right > r.left && r.bottom > r.top;
}

static int source_rect_fits(const Ps2SwMergeLayer *layer)
{
    const Ps2SwMergeRect r = layer->source;
    return valid_rect(r) && r.left >= 0 && r.top >= 0 &&
           (uint32_t)r.right <= layer->image.width &&
           (uint32_t)r.bottom <= layer->image.height;
}

typedef struct SampleAxis {
    uint32_t coordinate, step;
    uint64_t remainder, remainder_step, denominator;
} SampleAxis;

/* Evaluate the pixel-center equation once, then advance its exact quotient
 * and remainder. No rounded fixed-point scale or division in the pixel loop.
 * Signed destination endpoints can span INT32_MIN..INT32_MAX; the initial
 * numerator and every remainder operation still fit uint64_t. */
static SampleAxis sample_axis(int32_t pixel, int32_t destination_start,
                             int32_t destination_end, int32_t source_start,
                             int32_t source_end)
{
    const uint64_t offset = (uint64_t)((int64_t)pixel - destination_start);
    const uint64_t destination_extent = (uint64_t)((int64_t)destination_end - destination_start);
    const uint64_t source_extent = (uint64_t)((int64_t)source_end - source_start);
    const uint64_t denominator = 2u * destination_extent;
    const uint64_t numerator = (2u * offset + 1u) * source_extent;
    const uint64_t increment = 2u * source_extent;
    const SampleAxis result = {
        (uint32_t)source_start + (uint32_t)(numerator / denominator),
        (uint32_t)(increment / denominator),
        numerator % denominator, increment % denominator, denominator
    };
    return result;
}

static void sample_advance(SampleAxis *axis)
{
    axis->coordinate += axis->step;
    axis->remainder += axis->remainder_step;
    if (axis->remainder >= axis->denominator) {
        axis->remainder -= axis->denominator;
        ++axis->coordinate;
    }
}

static void draw_layer(const Ps2SwMergeOutput *output,
                       const Ps2SwMergeLayer *layer, int blend,
                       uint32_t mmod, uint32_t alp)
{
    const Ps2SwMergeRect r = layer->destination;
    const int32_t left = r.left > 0 ? r.left : 0;
    const int32_t top = r.top > 0 ? r.top : 0;
    const int32_t right = r.right < (int32_t)output->width ? r.right : (int32_t)output->width;
    const int32_t bottom = r.bottom < (int32_t)output->height ? r.bottom : (int32_t)output->height;
    if (left >= right || top >= bottom)
        return;
    const SampleAxis first_x = sample_axis(left, r.left, r.right,
                                           layer->source.left, layer->source.right);
    SampleAxis sample_y = sample_axis(top, r.top, r.bottom,
                                      layer->source.top, layer->source.bottom);
    int32_t y;
    for (y = top; y < bottom; ++y) {
        uint8_t *destination = output->pixels + (size_t)y * output->pitch;
        const uint8_t *source = layer->image.pixels + (size_t)sample_y.coordinate * layer->image.pitch;
        SampleAxis sample_x = first_x;
        int32_t x;
        for (x = left; x < right; ++x) {
            const uint8_t *s = source + (size_t)sample_x.coordinate * 4u;
            uint8_t *d = destination + (size_t)x * 4u;
            uint32_t weight = 255;
            unsigned channel;
            if (blend) {
                weight = mmod ? alp : 2u * s[3];
                if (weight > 255u)
                    weight = 255u;
            }
            /* Exact endpoints of the integer blend equation. Opaque pixels
             * need no destination reads or multiply/divide approximation. */
            if (weight == 255u) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
            } else if (weight != 0u) {
                for (channel = 0; channel < 3; ++channel)
                    d[channel] = (uint8_t)((s[channel] * weight +
                                           d[channel] * (255u - weight) + 127u) / 255u);
            }
            d[3] = 255;
            if (x + 1 < right)
                sample_advance(&sample_x);
        }
        if (y + 1 < bottom)
            sample_advance(&sample_y);
    }
}

int ps2_sw_merge_rgba(const Ps2SwMergeOutput *output,
                      const Ps2SwMergeParameters *parameters)
{
    Ps2SwMergeOutput target;
    Ps2SwMergeParameters config;
    StorageSpan destination;
    uint8_t background[3];
    uint32_t x, y;
    unsigned i;
    int result;
    if (!output || !parameters)
        return PS2_SW_MERGE_INVALID;
    /* Descriptors may themselves reside in the output allocation. Snapshot
     * them before any writes so that clearing pixels cannot alter this call. */
    target = *output;
    config = *parameters;
    if (config.modes)
        return PS2_SW_MERGE_UNSUPPORTED;
    if (config.mmod > 1u || config.slbg > 1u || config.alp > 255u)
        return PS2_SW_MERGE_INVALID;
    result = image_span(target.pixels, target.bytes, target.pitch,
                        target.width, target.height, &destination);
    if (result)
        return result;
    for (i = 0; i < 2; ++i) {
        const Ps2SwMergeLayer *layer = &config.circuit[i];
        StorageSpan source;
        if (layer->enabled > 1u)
            return PS2_SW_MERGE_INVALID;
        if (!layer->enabled)
            continue;
        result = image_span(layer->image.pixels, layer->image.bytes,
                            layer->image.pitch, layer->image.width,
                            layer->image.height, &source);
        if (result)
            return result;
        if (!source_rect_fits(layer) || !valid_rect(layer->destination))
            return PS2_SW_MERGE_INVALID;
        if (source.begin < destination.end && destination.begin < source.end)
            return PS2_SW_MERGE_ALIAS;
    }
    background[0] = (uint8_t)config.background_rgb;
    background[1] = (uint8_t)(config.background_rgb >> 8);
    background[2] = (uint8_t)(config.background_rgb >> 16);
    for (y = 0; y < target.height; ++y) {
        uint8_t *row = target.pixels + (size_t)y * target.pitch;
        for (x = 0; x < target.width; ++x) {
            row[(size_t)x * 4u] = background[0];
            row[(size_t)x * 4u + 1u] = background[1];
            row[(size_t)x * 4u + 2u] = background[2];
            row[(size_t)x * 4u + 3u] = 255;
        }
    }
    if (config.circuit[1].enabled && !config.slbg)
        draw_layer(&target, &config.circuit[1], 0, 0, 0);
    if (config.circuit[0].enabled)
        draw_layer(&target, &config.circuit[0], 1, config.mmod, config.alp);
    return PS2_SW_MERGE_OK;
}
