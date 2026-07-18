/* atomiX bare-metal link script: everything in the v1 128 KiB BRAM at
 * 0x80000000 (DESIGN.md §3.2). Phase 6's SDRAM platform will raise this
 * limit; keeping it exact today prevents trap stacks from escaping RTL RAM.
 * vaddr == paddr; no ELF headers mapped into memory. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
  RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128K
}

PHDRS {
  load PT_LOAD;
}

SECTIONS {
  .text : {
    *(.text.init)           /* reset entry first */
    *(.text*)
  } > RAM :load

  .rodata : { *(.rodata*) *(.srodata*) } > RAM :load
  .data   : { *(.data*) *(.sdata*) } > RAM :load

  /* riscv-tests-style HTIF exit word; kept in its own section so the
   * symbol is easy to find and align. */
  .tohost ALIGN(16) : { *(.tohost) } > RAM :load

  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*) *(.sbss*) *(COMMON)
    __bss_end = .;
  } > RAM

  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  _end = .;

  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
