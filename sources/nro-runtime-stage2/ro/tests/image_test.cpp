// SPDX-License-Identifier: GPL-2.0-only
#include "../source/image.h"
#include <assert.h>
#include <string.h>

static void write32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

int main(void) {
    uint8_t header[0x80] = {0};
    RoImageLayout layout;
    write32(header + 0x10, 0x304f524eu);
    write32(header + 0x18, 0x3000);
    write32(header + 0x24, 0x1000);
    write32(header + 0x28, 0x1000);
    write32(header + 0x2c, 0x1000);
    write32(header + 0x30, 0x2000);
    write32(header + 0x34, 0x1000);
    assert(roGetImageLayout(header, 0x3000, 0, &layout));
    assert(layout.flags == 0 && layout.text_size == 0x1000 && layout.ro_size == 0x1000);
    assert(!roGetImageLayout(header, 0x2000, 0, &layout));
    assert(!roGetImageLayout(header, 0x3000, 0x1000, &layout));
    write32(header + 0x30, 0x3000);
    assert(!roGetImageLayout(header, 0x3000, 0, &layout));
    write32(header + 0x30, 0x2000);
    write32(header + 0x1c, 1);
    assert(roGetImageLayout(header, 0x3000, 0, &layout) && layout.flags == 1);
    write32(header + 0x1c, 2);
    assert(!roGetImageLayout(header, 0x3000, 0, &layout));
    write32(header + 0x1c, 0);
    write32(header + 0x3c, 0x80);
    assert(!roGetImageLayout(header, 0x3000, 0, &layout));
    return 0;
}
