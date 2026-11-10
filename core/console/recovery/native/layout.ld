ENTRY(iz_net_main)
SECTIONS {
 . = 0;
 .text : { KEEP(*(.text.iz_net_main)) *(.text*) *(.rodata*) }
 .data : { *(.data*) *(.got*) }
 .bss : { *(.bss*) *(COMMON) }
 /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
