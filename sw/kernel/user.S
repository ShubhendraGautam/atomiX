/* The first aXos user payload deliberately needs no library or stack. It
 * enters S-mode once through ecall, then spends time in U-mode so CLINT-driven
 * preemption and resumption are exercised by the kernel test. */
  .section .usertext, "ax", @progbits
  .globl user_entry
  .type user_entry, @function
user_entry:
  li a7, 1                       /* SYS_USER_ONLINE */
  ecall
1:
  nop
  nop
  j 1b
  .size user_entry, . - user_entry
