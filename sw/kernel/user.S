/* The first aXos user payload deliberately needs no library or stack. It
 * enters S-mode once through ecall, then spends time in U-mode so CLINT-driven
 * preemption and resumption are exercised by the kernel test. */
  .section .usertext, "ax", @progbits
  .globl user_entry
  .type user_entry, @function
user_entry:
  lui t0, 0x40001                 /* private user stack: 0x40001000 */
  li t1, 0x51a00000
  or t1, t1, a0                   /* per-workload marker */
  sw t1, 0(t0)
  li a7, 1                       /* SYS_USER_ONLINE */
  ecall
  addi a0, a0, 'A'               /* process 0 prints A, process 1 prints B */
  li a7, 2                       /* SYS_CONSOLE_PUTC */
  ecall
  li a0, 0
  li a7, 3                       /* SYS_EXIT */
  ecall
1:
  nop
  nop
  j 1b
  .size user_entry, . - user_entry
