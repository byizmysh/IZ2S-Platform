#include "ps2_shell.h"

#ifndef PS2_SHELL_EXIT_LABEL
#define PS2_SHELL_EXIT_LABEL "CIRCLE TO EXIT"
#endif

static const uint8_t letters[26][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
 {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
 {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static const uint8_t two[7] = {14,17,1,2,4,8,31};

static void rectangle(const Ps2ShellHost* host, unsigned x, unsigned y,
                      unsigned width, unsigned height, uint32_t rgb) {
    if (x >= PS2_SHELL_WIDTH || y >= PS2_SHELL_HEIGHT)
        return;
    if (width > PS2_SHELL_WIDTH - x) width = PS2_SHELL_WIDTH - x;
    if (height > PS2_SHELL_HEIGHT - y) height = PS2_SHELL_HEIGHT - y;
    for (unsigned row = y; row < y + height; ++row) {
        uint8_t* pixel = host->pixels + (size_t)row * host->pitch + (size_t)x * 4;
        for (unsigned column = 0; column < width; ++column, pixel += 4) {
            pixel[0] = (uint8_t)(rgb >> 16);pixel[1] = (uint8_t)(rgb >> 8);
            pixel[2] = (uint8_t)rgb;pixel[3] = 255;
        }
    }
}
static unsigned text_width(const char* text, unsigned scale) {
    unsigned count = 0;
    while (text[count]) ++count;
    return count ? count * 6 * scale - scale : 0;
}
static void text(const Ps2ShellHost* host, unsigned x, unsigned y,
                const char* value, unsigned scale, uint32_t rgb) {
    for (; *value; ++value, x += 6 * scale) {
        const uint8_t* glyph = *value >= 'A' && *value <= 'Z' ? letters[*value - 'A'] :
            (*value == '2' ? two : (const uint8_t*)0);
        if (!glyph) continue;
        for (unsigned row = 0; row < 7; ++row)
            for (unsigned column = 0; column < 5; ++column)
                if (glyph[row] & (1u << (4 - column)))
                    rectangle(host, x + column * scale, y + row * scale, scale, scale, rgb);
    }
}
static void centered(const Ps2ShellHost* host, unsigned y, const char* value,
                     unsigned scale, uint32_t rgb) {
    text(host, (PS2_SHELL_WIDTH - text_width(value, scale)) / 2, y, value, scale, rgb);
}
static void render(const Ps2ShellHost* host) {
    rectangle(host, 0, 0, PS2_SHELL_WIDTH, PS2_SHELL_HEIGHT, 0x0c1426);
    rectangle(host, 96, 104, 768, 2, 0x2a4564);
    rectangle(host, 96, 104, 96, 2, 0x78c1ff);
    text(host, 96, 73, "PLAYSTATION 2", 2, 0x91bad9);
    rectangle(host, 96, 150, 768, 218, 0x122139);
    centered(host, 190, "PLAYSTATION 2", 6, 0xf3f7ff);
    centered(host, 264, "NO GAME LOADED", 3, 0x9bcfff);
    centered(host, 319, "NO GAME IS RUNNING", 2, 0x91a9c4);
    rectangle(host, 96, 418, 768, 1, 0x2a4564);
    centered(host, 451, PS2_SHELL_EXIT_LABEL, 2, 0xdbe9f5);
}
static int span(uintptr_t pointer, size_t bytes) {
    return pointer && bytes && bytes <= UINTPTR_MAX - pointer;
}
static int separate(uintptr_t a, size_t n, uintptr_t b, size_t m) {
    return a + n <= b || b + m <= a;
}
static void publish(Ps2ShellStatus* output, const Ps2ShellStatus* status) {
    *output = *status;
}
int ps2_shell_run(const Ps2ShellHost* supplied, Ps2ShellStatus* output) {
    if (!supplied || !output || (uintptr_t)supplied % _Alignof(Ps2ShellHost) ||
        (uintptr_t)output % _Alignof(Ps2ShellStatus) ||
        output->version != PS2_SHELL_VERSION || output->size != sizeof(*output))
        return PS2_SHELL_ARGUMENT;
    // Reject an older/short host prefix before copying fields it does not own.
    if (supplied->version != PS2_SHELL_VERSION || supplied->size != sizeof(*supplied))
        return PS2_SHELL_ARGUMENT;
    if (!span((uintptr_t)supplied, sizeof(*supplied)) || !span((uintptr_t)output, sizeof(*output)) ||
        !separate((uintptr_t)supplied, sizeof(*supplied), (uintptr_t)output, sizeof(*output)))
        return PS2_SHELL_ARGUMENT;
    const Ps2ShellHost host = *supplied;
    Ps2ShellStatus status = {PS2_SHELL_VERSION, sizeof(status), PS2_SHELL_ARGUMENT,
        PS2_SHELL_NOT_STARTED, PS2_SHELL_EXIT_NONE, 0, 0, 0, 0, 0, 0, 0};
    if (host.version != PS2_SHELL_VERSION || host.size != sizeof(host) ||
        !host.present || !host.poll || !host.wait || !host.finish ||
        host.pitch < (size_t)PS2_SHELL_WIDTH * 4 ||
        host.pitch > (SIZE_MAX - (size_t)PS2_SHELL_WIDTH * 4) / (PS2_SHELL_HEIGHT - 1u) ||
        host.bytes < (PS2_SHELL_HEIGHT - 1u) * host.pitch + (size_t)PS2_SHELL_WIDTH * 4 ||
        !span((uintptr_t)host.pixels, host.bytes) ||
        !separate((uintptr_t)host.pixels, host.bytes, (uintptr_t)supplied, sizeof(*supplied)) ||
        !separate((uintptr_t)host.pixels, host.bytes, (uintptr_t)output, sizeof(*output))) {
        // An aliased output could be part of the rejected pixel allocation.
        // Do not modify either descriptor/storage on any argument rejection.
        return PS2_SHELL_ARGUMENT;
    }
    render(&host);
    status.result = PS2_SHELL_OK;status.lifecycle = PS2_SHELL_ACTIVE;
    status.ownership_retained = 1;
    publish(output, &status);
    const Ps2ShellFrame frame = {PS2_SHELL_VERSION, sizeof(frame), host.pixels,
        host.bytes, host.pitch, PS2_SHELL_WIDTH, PS2_SHELL_HEIGHT};
    int error = host.present(host.context, &frame);
    if (error) {
        status.result = PS2_SHELL_PRESENT_FAILED;status.operation_error = error;
    } else {
        ++status.presented;
        unsigned circle_armed = 0, event_armed = 0;
        uint32_t previous_buttons = 0;
        for (;;) {
            Ps2ShellInput input = {PS2_SHELL_VERSION, sizeof(input), 0, 0, PS2_SHELL_EVENT_NONE, 0};
            ++status.polls;
            error = host.poll(host.context, &input);
            if (error) {
                status.result = PS2_SHELL_POLL_FAILED;status.operation_error = error;break;
            }
            if (input.version != PS2_SHELL_VERSION || input.size != sizeof(input) ||
                input.connected > 1 || input.event > PS2_SHELL_EVENT_EXIT || input.reserved) {
                status.result = PS2_SHELL_INPUT_INVALID;break;
            }
            const uint32_t buttons = input.connected ? input.buttons : 0;
            if (!input.connected) circle_armed = 0;
            if (!buttons && input.event == PS2_SHELL_EVENT_NONE) {
                event_armed = 1;
                if (input.connected) circle_armed = 1;
            }
            if (input.event == PS2_SHELL_EVENT_EXIT && event_armed) {
                status.exit_reason = PS2_SHELL_EXIT_HOST;break;
            }
            if (circle_armed && (buttons & ~previous_buttons & PS2_SHELL_CIRCLE)) {
                status.exit_reason = PS2_SHELL_EXIT_CIRCLE;break;
            }
            previous_buttons = buttons;
            ++status.waits;
            error = host.wait(host.context, 16);
            if (error) {
                status.result = PS2_SHELL_WAIT_FAILED;status.operation_error = error;break;
            }
            publish(output, &status);
        }
    }
    ++status.finish_calls;
    error = host.finish(host.context);
    if (error) {
        status.finish_error = error;status.result = PS2_SHELL_FINISH_UNPROVED;
        status.lifecycle = PS2_SHELL_RETAINED;
    } else {
        status.ownership_retained = 0;status.lifecycle = PS2_SHELL_FINISHED;
    }
    publish(output, &status);
    return (int)status.result;
}
