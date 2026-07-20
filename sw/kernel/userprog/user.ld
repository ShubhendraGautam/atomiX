/* User programs are linked at USER_CODE_VA (0x40000000), the base of the 4 MiB
 * region the Sv32 user address space maps.  Segments are page-aligned so the
 * loader can give each its own permissions; without that, .text and .rodata
 * would share a page and have to share a permission set. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY { USER (rwx) : ORIGIN = 0x40000000, LENGTH = 4M - 4K }

SECTIONS
{
  .text : ALIGN(4K) { *(.text._start) *(.text*) } > USER
  .rodata : ALIGN(4K) { *(.rodata*) *(.srodata*) } > USER
  .data : ALIGN(4K) { *(.data*) *(.sdata*) } > USER
  .bss : ALIGN(16) { *(.bss*) *(.sbss*) *(COMMON) } > USER
  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) }
}
