#pragma once
extern "C" const u8 izps2_checkpoint_burst_seed[330680];
asm(R"seed(.pushsection .rodata.izps2_checkpoint_burst_seed, "a", %progbits
.global izps2_checkpoint_burst_seed
.hidden izps2_checkpoint_burst_seed
.type izps2_checkpoint_burst_seed, %object
izps2_checkpoint_burst_seed:
.incbin "${IZPS2_WORKSPACE}/IZ-R799/run25/jit-single-range/R1235-gta-checkpoint-compile-burst/checkpoint-burst-seed.profile"
.size izps2_checkpoint_burst_seed, . - izps2_checkpoint_burst_seed
.popsection)seed");
