#include "ps2_sb4_platform.h"

int ps2_sb4_platform_attach(IzNativeApp *app, Ps2Sb4Platform *out) {
    if (!out) return 0;
    *out = (Ps2Sb4Platform){0};
    Ps2Sb4Vulkan graphics;
    if (!ps2_sb4_vulkan_bind(app, &graphics) || !ps2_sb4_sd_saves_attach(app)) return 0;
    out->files = ps2_sb4_sd_saves_files();
    out->vulkan = graphics;
    ps2_sb4_pad_configure(app, &out->pad);
    return 1;
}
