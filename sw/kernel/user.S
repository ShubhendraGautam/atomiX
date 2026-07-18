/* The first aXos user payload deliberately needs no library or stack. It
 * enters S-mode once through ecall, then spends time in U-mode so CLINT-driven
 * preemption and resumption are exercised by the kernel test. */
  .section .usertext, "ax", @progbits
  .globl user_entry
  .type user_entry, @function
user_entry:
  lui t0, 0x40001                 /* private user stack: 0x40001000 */
  li t1, 0x51a00000
  ori t1, t1, 1                   /* marker copied into the child stack */
  sw t1, 0(t0)
  li a7, 1                       /* SYS_FORK */
  ecall
  bnez a0, 1f
  li a0, 'C'
  li a7, 2                       /* SYS_CONSOLE_PUTC */
  ecall
  li a0, 0
  li a7, 3                       /* SYS_EXIT */
  ecall
0:
  nop
  j 0b
1:
  li a0, 'P'
  li a7, 2                       /* SYS_CONSOLE_PUTC */
  ecall
  li a7, 4                       /* SYS_WAIT */
  ecall
  li a0, 'W'
  li a7, 2                       /* SYS_CONSOLE_PUTC */
  ecall
  li a0, 0
  li a7, 3                       /* SYS_EXIT */
  ecall
  j 0b
  .size user_entry, . - user_entry
