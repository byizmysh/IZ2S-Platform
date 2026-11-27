// SPDX-License-Identifier: GPL-2.0-only
#include "image.h"

#include <cstring>

namespace {

constexpr u32 NroMagic = 0x304f524e; // "NRO0", little-endian.
constexpr u64 PageSize = 0x1000;

// NRO header fields.
constexpr u32 NroHeader_Magic = 0x10;
constexpr u32 NroHeader_Version = 0x14;
constexpr u32 NroHeader_ImageSize = 0x18;
constexpr u32 NroHeader_Flags = 0x1c;
constexpr u32 NroHeader_TextOffset = 0x20;
constexpr u32 NroHeader_TextSize = 0x24;
constexpr u32 NroHeader_RoOffset = 0x28;
constexpr u32 NroHeader_RoSize = 0x2c;
constexpr u32 NroHeader_DataOffset = 0x30;
constexpr u32 NroHeader_DataSize = 0x34;
constexpr u32 NroHeader_BssSize = 0x38;
constexpr u32 NroHeader_Reserved = 0x3c;
constexpr u32 NroHeader_ModuleId = 0x40;

bool isPageAligned(u64 size) {
    return (size & (PageSize - 1)) == 0;
}

// The header need not be aligned, so each field is copied out.
u32 read32(const u8* header, u32 offset) {
    u32 value;
    std::memcpy(&value, header + offset, sizeof(value));
    return value;
}

} // namespace

bool roGetImageLayout(const u8 header[0x80], u64 image_size, u64 bss_size, RoImageLayout* out) {
    if (!header || !out)
        return false;

    if (read32(header, NroHeader_Magic) != NroMagic || read32(header, NroHeader_Version) != 0)
        return false;

    const u32 flags = read32(header, NroHeader_Flags);
    if (flags > RoNroFlag_ReadOnlyHeader || read32(header, NroHeader_Reserved) != 0)
        return false;

    if (read32(header, NroHeader_ImageSize) != image_size || read32(header, NroHeader_BssSize) != bss_size)
        return false;
    if (!image_size || !isPageAligned(image_size) || !isPageAligned(bss_size))
        return false;

    const u32 text_offset = read32(header, NroHeader_TextOffset);
    const u32 text_size = read32(header, NroHeader_TextSize);
    const u32 ro_offset = read32(header, NroHeader_RoOffset);
    const u32 ro_size = read32(header, NroHeader_RoSize);
    const u32 data_offset = read32(header, NroHeader_DataOffset);
    const u32 data_size = read32(header, NroHeader_DataSize);
    if (text_offset != 0 || !text_size || !ro_size || !data_size)
        return false;
    if (!isPageAligned(text_size) || !isPageAligned(ro_size) || !isPageAligned(data_size))
        return false;

    // Each segment begins where the previous one ends, and data ends the image.
    if (ro_offset != text_size || static_cast<u64>(data_offset) != static_cast<u64>(ro_offset) + ro_size ||
        image_size != static_cast<u64>(data_offset) + data_size)
        return false;

    *out = RoImageLayout{flags, text_size, ro_size, data_size, static_cast<u32>(bss_size), {}};
    std::memcpy(out->module_id, header + NroHeader_ModuleId, sizeof(out->module_id));
    return true;
}
