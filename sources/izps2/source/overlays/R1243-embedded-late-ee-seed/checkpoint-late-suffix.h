#pragma once
extern "C" const u8 izps2_checkpoint_late_suffix[10672];
asm(R"seed(.pushsection .rodata.izps2_checkpoint_late_suffix, "a", %progbits
.global izps2_checkpoint_late_suffix
.hidden izps2_checkpoint_late_suffix
.type izps2_checkpoint_late_suffix, %object
izps2_checkpoint_late_suffix:
.incbin "${IZPS2_WORKSPACE}/IZ-R799/run25/jit-single-range/R1243-embedded-late-ee-seed/checkpoint-late-suffix.bin"
.size izps2_checkpoint_late_suffix, . - izps2_checkpoint_late_suffix
.popsection)seed");
