OUTPUT_ARCH(aarch64)
ENTRY(ps2_sb4_hosted_entry)

PHDRS {
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x100000000;
    .text : {
        KEEP(*(.text.ps2_sb4_hosted_entry))
        *(.text.unlikely* .text.exit* .text.startup* .text.hot*)
        *(.text .text.* .stub .gnu.linkonce.t.*)
    } :code

    .plt : { *(.plt .iplt) } :code
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) } :code
    .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } :code
    .eh_frame_hdr : { *(.eh_frame_hdr) } :code
    .eh_frame : { KEEP(*(.eh_frame)) *(.eh_frame.*) } :code

    /* These static pointers are relocated before the loader publishes RX.
       Retain PC-relative metadata and GOT accesses within the same segment. */
    .data.rel.ro : {
        *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*)
        *(.data.DW.ref.* .data.rel.local.DW.ref.*)
    } :code
    .got : { *(.got .igot) } :code

    /* Leave room for the portable image page-address thunks. */
    . = ALIGN(4096) + 0x100000;
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } :data
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :data
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } :data

    .got.plt : { *(.got.plt .igot.plt) } :data
    .data : { *(.data .data.* .gnu.linkonce.d.*) } :data
    .bss : { *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON) } :data

    .nro_startup : { *(.crt0 .init .fini .dynamic .interp) } :data
    .native_tls : { *(.tdata .tdata.* .tbss .tbss.* .tcommon) } :data
    ASSERT(SIZEOF(.nro_startup) == 0, "Hosted engine contains standalone startup sections")
    ASSERT(SIZEOF(.native_tls) == 0, "Hosted engine requires thread-local storage from its own process")

    /DISCARD/ : { *(.group .comment .note .note.*) }
}
