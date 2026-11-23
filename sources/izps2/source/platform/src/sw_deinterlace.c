#include "sw_deinterlace.h"

typedef struct Span { uintptr_t begin, end; } Span;

static int span(const void *pointer, size_t bytes, Span *out) {
    const uintptr_t p = (uintptr_t)pointer;
    if (!p || !bytes || bytes > UINTPTR_MAX - p) return 0;
    out->begin = p;out->end = p + bytes;return 1;
}
static int overlap(Span a, Span b) {
    return a.begin < b.end && b.begin < a.end;
}
static int descriptor(const void *p, size_t bytes, size_t alignment, Span *out) {
    return (uintptr_t)p % alignment == 0 && span(p, bytes, out);
}
static int image(const void *pixels, size_t bytes, size_t pitch,
                 uint32_t width, uint32_t height, Span *out) {
    const size_t row = (size_t)width * 4u;
    size_t needed;
    if (pitch < row || (size_t)(height - 1u) > (SIZE_MAX - row) / pitch)
        return 0;
    needed = (size_t)(height - 1u) * pitch + row;
    return bytes >= needed && span(pixels, needed, out);
}
static unsigned difference(const uint8_t *a, const uint8_t *b) {
    unsigned greatest = 0;
    for (unsigned c = 0; c < 3; ++c) {
        const unsigned d = a[c] > b[c] ? a[c] - b[c] : b[c] - a[c];
        if (d > greatest) greatest = d;
    }
    return greatest;
}
static const uint8_t *at(const uint8_t *banks, uint32_t width, uint32_t height,
                         unsigned bank, unsigned x, unsigned y) {
    return banks + (((size_t)bank * height + y) * width + x) * 4u;
}

int ps2_sw_deinterlace_reset(Ps2SwDeinterlaceState *state) {
    Span unused;
    if (!descriptor(state, sizeof(*state), _Alignof(Ps2SwDeinterlaceState), &unused))
        return PS2_DEINTERLACE_ARGUMENT;
    *state = (Ps2SwDeinterlaceState){.version = PS2_SW_DEINTERLACE_VERSION,
                                   .size = sizeof(*state)};
    return PS2_DEINTERLACE_OK;
}

int ps2_sw_deinterlace_fastmad(Ps2SwDeinterlaceState *state,
    const Ps2SwMergeImage *merged, const Ps2SwMergeOutput *output,
    const Ps2SwDeinterlaceParameters *parameters,
    void *history, size_t history_bytes, Ps2SwDeinterlaceInfo *info) {
    Span descriptors[5], input_span, output_span, history_span, provided_history;
    unsigned descriptor_count = 4;
    if (!descriptor(state, sizeof(*state), _Alignof(Ps2SwDeinterlaceState), &descriptors[0]) ||
        !descriptor(merged, sizeof(*merged), _Alignof(Ps2SwMergeImage), &descriptors[1]) ||
        !descriptor(output, sizeof(*output), _Alignof(Ps2SwMergeOutput), &descriptors[2]) ||
        !descriptor(parameters, sizeof(*parameters), _Alignof(Ps2SwDeinterlaceParameters), &descriptors[3]))
        return PS2_DEINTERLACE_ARGUMENT;
    if (info) {
        if (!descriptor(info, sizeof(*info), _Alignof(Ps2SwDeinterlaceInfo), &descriptors[4]))
            return PS2_DEINTERLACE_ARGUMENT;
        descriptor_count = 5;
    }
    for (unsigned i = 0; i < descriptor_count; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (overlap(descriptors[i], descriptors[j])) return PS2_DEINTERLACE_ALIAS;

    const Ps2SwMergeImage input = *merged;
    const Ps2SwMergeOutput target = *output;
    const Ps2SwDeinterlaceParameters options = *parameters;
    if (state->version != PS2_SW_DEINTERLACE_VERSION || state->size != sizeof(*state) ||
        state->initialized > 1u || (state->initialized &&
        (!state->width || state->width > PS2_SW_DEINTERLACE_MAX_WIDTH ||
         !state->height || state->height > PS2_SW_DEINTERLACE_MAX_HEIGHT ||
         state->index > 3u || !state->valid_fields || state->valid_fields > 4u ||
         state->last_field > 1u || (state->index & 1u) != state->last_field ||
         !state->history))) return PS2_DEINTERLACE_STATE;
    if (!input.width || !input.height || input.width > PS2_SW_DEINTERLACE_MAX_WIDTH ||
        input.height > PS2_SW_DEINTERLACE_MAX_HEIGHT ||
        target.width != input.width || target.height != input.height ||
        options.field > 1u || options.pad_first > options.pad_end ||
        options.pad_end > input.height || options.reserved) return PS2_DEINTERLACE_ARGUMENT;
    const size_t required = (size_t)input.width * input.height * 8u;
    if (!image(input.pixels, input.bytes, input.pitch, input.width, input.height, &input_span) ||
        !image(target.pixels, target.bytes, target.pitch, target.width, target.height, &output_span) ||
        history_bytes < required || !span(history, history_bytes, &provided_history) ||
        !span(history, required, &history_span)) return PS2_DEINTERLACE_STORAGE;
    if ((overlap(input_span, output_span) &&
         !(input.pixels == target.pixels && input.pitch == target.pitch)) ||
        overlap(input_span, history_span) || overlap(output_span, history_span))
        return PS2_DEINTERLACE_ALIAS;
    for (unsigned i = 0; i < descriptor_count; ++i)
        if (overlap(descriptors[i], input_span) || overlap(descriptors[i], output_span) ||
            overlap(descriptors[i], history_span)) return PS2_DEINTERLACE_ALIAS;

    const unsigned reset = !state->initialized || state->width != input.width ||
        state->height != input.height || state->history != (uintptr_t)history ||
        state->history_bytes != history_bytes || state->sequence == UINT64_MAX ||
        options.sequence != state->sequence + 1u || state->last_field == options.field;
    uint8_t *banks = history;
    if (reset) for (size_t i = 0; i < required; ++i) banks[i] = 0;
    const unsigned previous_index = reset ? 0u : state->index;
    const unsigned index = (((previous_index + 1u) & ~1u) | options.field) & 3u;
    const unsigned bank = index >> 1;

    /* The shader's odd-height bank lofs makes parity local to each bank.
     * All source reads finish before any output write (including in-place). */
    for (unsigned y = options.field; y < input.height; y += 2u) {
        unsigned source_y = y;
        if (y >= options.pad_first && y < options.pad_end) {
            source_y = options.pad_end;
            if (source_y == input.height) source_y = input.height - 1u;
        }
        const uint8_t *source = input.pixels + (size_t)source_y * input.pitch;
        uint8_t *destination = banks + ((size_t)bank * input.height + y) * input.width * 4u;
        for (size_t x = 0; x < (size_t)input.width * 4u; ++x) destination[x] = source[x];
    }

    /* Exact bank ordering of interlace.glsl ps_main4, indexed by GSDevice's
     * two-bank/four-field ring. Newest high/low, center, old high/low, old center. */
    static const uint8_t order[4][4] = {{0,1,1,0},{0,0,1,1},{1,0,0,1},{1,1,0,0}};
    for (unsigned y = 0; y < input.height; ++y) {
        uint8_t *row = target.pixels + (size_t)y * target.pitch;
        for (unsigned x = 0; x < input.width; ++x) {
            const uint8_t *selected;
            unsigned average = 0;
            const uint8_t *high = 0, *low = 0;
            if ((y & 1u) == options.field) {
                selected = at(banks,input.width,input.height,order[index][0],x,y);
            } else {
                selected = at(banks,input.width,input.height,order[index][1],x,y);
                /* The shader's strict normalized texture-coordinate tests
                 * always weave the first and last destination rows. */
                if (y && y + 1u < input.height) {
                    high = at(banks,input.width,input.height,order[index][0],x,y-1u);
                    low = at(banks,input.width,input.height,order[index][0],x,y+1u);
                    const uint8_t *old_high = at(banks,input.width,input.height,order[index][2],x,y-1u);
                    const uint8_t *old_low = at(banks,input.width,input.height,order[index][2],x,y+1u);
                    const uint8_t *old_center = at(banks,input.width,input.height,order[index][3],x,y);
                    const unsigned mh = difference(high,old_high);
                    const unsigned mc = difference(selected,old_center);
                    const unsigned ml = difference(low,old_low);
                    if (mh >= 21u || mc >= 21u || ml >= 21u) average = 1;
                    else if (mh || mc || ml) {
                        const unsigned hl = difference(high,low);
                        const unsigned hc = difference(high,selected);
                        /* sensitivity .08*255=20.4; normalized shader's
                         * HC-.08 >= .90*(HL-.08) becomes 10HC-9HL >=21. */
                        average = hl <= 20u && (int)(10u*hc) - (int)(9u*hl) >= 21;
                    }
                }
            }
            for (unsigned c = 0; c < 4; ++c)
                row[(size_t)x*4u+c] = average ?
                    (uint8_t)(((unsigned)high[c]+low[c]+1u)/2u) : selected[c];
        }
    }
    const unsigned fields = reset ? 1u : state->valid_fields < 4u ? state->valid_fields + 1u : 4u;
    state->initialized = 1;state->width = input.width;state->height = input.height;
    state->index = index;state->valid_fields = fields;state->last_field = options.field;
    state->sequence = options.sequence;state->history = (uintptr_t)history;state->history_bytes = history_bytes;
    if (info) *info = (Ps2SwDeinterlaceInfo){required,reset,index,bank,options.field,fields,
        reset ? "fastmad-zero-history-reset" : "fastmad-four-field-history"};
    return PS2_DEINTERLACE_OK;
}
