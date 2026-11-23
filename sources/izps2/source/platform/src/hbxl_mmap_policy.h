#ifndef PS2_HBXL_MMAP_POLICY_H
#define PS2_HBXL_MMAP_POLICY_H

/* Linux/AArch64 mmap flag values. This adapter allocates independent buffers;
 * it cannot supply shared aliases or honor a fixed virtual address. Reject
 * those requests before allocating or reading a file, including address zero
 * and MAP_FIXED_NOREPLACE. Returning a fresh buffer would corrupt emulated GS
 * memory even when every allocation and file read succeeds. */
static inline int ps2_hbxl_mmap_kind_supported(unsigned long flags)
{
    const unsigned long map_type = 0x0f;
    const unsigned long map_private = 0x02;
    const unsigned long map_fixed = 0x10;
    const unsigned long map_fixed_noreplace = 0x100000;
    return (flags & map_type) == map_private &&
           !(flags & (map_fixed | map_fixed_noreplace));
}

#endif
