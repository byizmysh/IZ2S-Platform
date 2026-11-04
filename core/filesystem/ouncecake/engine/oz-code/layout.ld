ENTRY(ftp_main)
SECTIONS {
  . = 0;
  .text : { KEEP(*(.text.ftp_main)) *(.text*) *(.rodata*) }
  .data : { *(.data*) *(.got*) }
  .bss : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
