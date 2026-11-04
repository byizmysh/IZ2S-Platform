ENTRY(_start)
SECTIONS {
    . = 0;
    .text : { KEEP(*(.text.start)) *(.text .text.*) }
    . = ALIGN(0x1000);
    .rodata : { KEEP(*(.rodata.module)) *(.rodata .rodata.*) }
    . = ALIGN(0x1000);
    .data : { KEEP(*(.data .data.*)) }
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    __bss_end = .;
    /DISCARD/ : { *(.comment .note* .eh_frame*) }
}
