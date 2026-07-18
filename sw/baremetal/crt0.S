/* Minimal RV32 reset code. The linker keeps this section first at the RAM
 * load address; no OpenSBI or runtime library is required. */
  .section .text.init, "ax", @progbits
  .globl _start
  .type _start, @function
_start:
  .option push
  .option norelax
  la sp, __stack_top
  .option pop

  la t0, __bss_start
  la t1, __bss_end
1:
  bgeu t0, t1, 2f
  sw zero, 0(t0)
  addi t0, t0, 4
  j 1b
2:
  call main
3:
  wfi
  j 3b

  .size _start, . - _start
