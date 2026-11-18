ENTRY(iz_app)
SECTIONS {
    . = 0;
    .text : { KEEP(*(.text.iz_app)) *(.text*) *(.rodata*) . = ALIGN(4); }
    /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
    ASSERT(SIZEOF(.data) == 0, "native image must not contain writable static data")
    ASSERT(SIZEOF(.bss) == 0, "native state must live in the app allocation")
}
