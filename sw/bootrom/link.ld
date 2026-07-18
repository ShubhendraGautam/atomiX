OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
  ROM (rx) : ORIGIN = 0x00001000, LENGTH = 4K
  /* Scratch lives above the initial kernel image while the ROM loader runs. */
  SCRATCH (rw) : ORIGIN = 0x80010000, LENGTH = 4K
}
SECTIONS {
  .text : { *(.text.init) *(.text*) } > ROM
  .rodata : { *(.rodata*) *(.srodata*) } > ROM
  .data : { *(.data*) *(.sdata*) } > ROM
  .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) } > SCRATCH
  __stack_top = ORIGIN(SCRATCH) + LENGTH(SCRATCH);
  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
