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

  .rodata : { *(.rodata*) *(.srodata*) } > RAM :load
  .data   : { *(.data*) *(.sdata*) } > RAM :load

  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*) *(.sbss*) *(COMMON)
    __bss_end = .;
  } > RAM

  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  _end = .;

  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
