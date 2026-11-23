#pragma once
extern "C" const u8 izps2_extended_stall_seed[308344];
asm(R"seed(.pushsection .rodata.izps2_extended_stall_seed, "a", %progbits
.global izps2_extended_stall_seed
.hidden izps2_extended_stall_seed
.type izps2_extended_stall_seed, %object
izps2_extended_stall_seed:
.incbin "${IZPS2_WORKSPACE}/IZ-R799/run25/jit-single-range/R1228-ee-warm-startup-bursts/model/extended-seed.profile"
.size izps2_extended_stall_seed, . - izps2_extended_stall_seed
.popsection)seed");
