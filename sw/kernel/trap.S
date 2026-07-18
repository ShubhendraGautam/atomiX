/* No scheduler exists in the bootstrap yet. A trap is therefore a test
 * failure, but making the path explicit proves that S-mode stvec is live. */
  .section .text, "ax", @progbits
  .globl supervisor_trap
  .type supervisor_trap, @function
supervisor_trap:
  li t0, 0x00100000
  li t1, 0x00013333
  sw t1, 0(t0)
1:
  wfi
  j 1b
  .size supervisor_trap, . - supervisor_trap
