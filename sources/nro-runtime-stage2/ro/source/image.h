// SPDX-License-Identifier: GPL-2.0-only
// The NRO header layout that RO loads.
#pragma once

#include <switch/types.h>

/// NRO header flag: the first page (the header) stays read-only, and text
/// starts after it.
constexpr u32 RoNroFlag_ReadOnlyHeader = 1;

struct RoImageLayout {
    u32 flags;
    u32 text_size;
    u32 ro_size;
    u32 data_size;
    u32 bss_size;
    u8 module_id[32];
};

/**
 * @brief Reads the layout of an ordinary NRO0: contiguous, page-aligned text,
 *        ro and data segments that fill the image, flags 0 or
 *        RoNroFlag_ReadOnlyHeader. Compressed images (flag 2) are not
 *        supported. This is not stock RO's authenticity check.
 *        Stock RO accepts the same two flags.
 * @param[in] header The first 0x80 bytes of the image.
 * @param[in] image_size, bss_size The sizes that the client passed; the header must declare the same.
 * @return Whether the image has that layout.
 */
bool roGetImageLayout(const u8 header[0x80], u64 image_size, u64 bss_size, RoImageLayout* out);
