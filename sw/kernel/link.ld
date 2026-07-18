/* Initial aXos image: execute from the shared 128 KiB RAM window.  Keeping
 * vaddr == paddr lets M-mode construct the first identity map before S-mode
 * begins. A higher-half layout can be introduced once process VM exists. */
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
    *(.text.init)
    *(.text*)
  } > RAM :load

  .usertext : { *(.usertext) } > RAM :load
  .rodata : { *(.rodata*) *(.srodata*) } > RAM :load
  .data   : { *(.data*) *(.sdata*) } > RAM :load

  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*) *(.sbss*) *(COMMON)
    __bss_end = .;
  } > RAM

  /* Reserve one page for the initial M/S/trap stack. The allocator owns only
   * pages below this boundary, never the live bootstrap stack. */
  __stack_bottom = ORIGIN(RAM) + LENGTH(RAM) - 4K;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  _end = .;

  ASSERT(ADDR(.usertext) + SIZEOF(.usertext) <= 0x80001000,
         "user payload must fit the kernel's first 4 KiB page")

  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
