ENTRY(store_engine)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x48000000;
    .text : { KEEP(*(.text.store_engine)) *(.text .text.*) } :code
    .rodata : { *(.rodata .rodata.*) } :code
    . = 0x4c000000;
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
