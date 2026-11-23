ENTRY(iz_app)
SECTIONS {
  . = 0;
  .text : {
    KEEP(*(.text.iz_app))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  }
  .data : { *(.data*) *(.got*) }
  .bss : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
  ASSERT(iz_app == 0, "IZlauncher calls offset zero")
  ASSERT(SIZEOF(.data) + SIZEOF(.bss) == 0, "Native frontend state belongs in app->data")
}
