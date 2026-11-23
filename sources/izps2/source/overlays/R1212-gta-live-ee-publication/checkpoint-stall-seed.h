#pragma once

extern "C" const u8 izps2_stall_seed[262168];
static constexpr GSCacheFile::Digest kStallSeedStamp{{0x9a, 0xea, 0x6c, 0xa0, 0x1a, 0x8b, 0x6c, 0x12, 0xaa, 0x58, 0xa4, 0x26, 0x71, 0x65, 0xf2, 0x01}};
asm(R"seed(.pushsection .rodata.izps2_stall_seed, "a", %progbits
.global izps2_stall_seed
.hidden izps2_stall_seed
.type izps2_stall_seed, %object
izps2_stall_seed:
.incbin "${IZPS2_WORKSPACE}/IZ-R799/run25/jit-single-range/R1193-checkpoint-stall-seed/model/extended-seed.profile"
.size izps2_stall_seed, . - izps2_stall_seed
.popsection)seed");
