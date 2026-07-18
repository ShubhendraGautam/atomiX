/* First S-mode trap frame. Saving every GPR makes this entry a sound base for
 * preemption: the C handler sees an ordinary call ABI and SRET restores the
 * interrupted supervisor context exactly. */
  .section .text, "ax", @progbits
  .globl supervisor_trap_entry
  .type supervisor_trap_entry, @function
supervisor_trap_entry:
  addi sp, sp, -128
  sw ra,   0(sp)
  sw gp,   4(sp)
  sw tp,   8(sp)
  sw t0,  12(sp)
  sw t1,  16(sp)
  sw t2,  20(sp)
  sw s0,  24(sp)
  sw s1,  28(sp)
  sw a0,  32(sp)
  sw a1,  36(sp)
  sw a2,  40(sp)
  sw a3,  44(sp)
  sw a4,  48(sp)
  sw a5,  52(sp)
  sw a6,  56(sp)
  sw a7,  60(sp)
  sw s2,  64(sp)
  sw s3,  68(sp)
  sw s4,  72(sp)
  sw s5,  76(sp)
  sw s6,  80(sp)
  sw s7,  84(sp)
  sw s8,  88(sp)
  sw s9,  92(sp)
  sw s10, 96(sp)
  sw s11,100(sp)
  sw t3, 104(sp)
  sw t4, 108(sp)
  sw t5, 112(sp)
  sw t6, 116(sp)
  mv a0, sp
  call supervisor_trap
  mv sp, a0
  lw ra,   0(sp)
  lw gp,   4(sp)
  lw tp,   8(sp)
  lw t0,  12(sp)
  lw t1,  16(sp)
  lw t2,  20(sp)
  lw s0,  24(sp)
  lw s1,  28(sp)
  lw a0,  32(sp)
  lw a1,  36(sp)
  lw a2,  40(sp)
  lw a3,  44(sp)
  lw a4,  48(sp)
  lw a5,  52(sp)
  lw a6,  56(sp)
  lw a7,  60(sp)
  lw s2,  64(sp)
  lw s3,  68(sp)
  lw s4,  72(sp)
  lw s5,  76(sp)
  lw s6,  80(sp)
  lw s7,  84(sp)
  lw s8,  88(sp)
  lw s9,  92(sp)
  lw s10, 96(sp)
  lw s11,100(sp)
  lw t3, 104(sp)
  lw t4, 108(sp)
  lw t5, 112(sp)
  lw t6, 116(sp)
  addi sp, sp, 128
  sret
  .size supervisor_trap_entry, . - supervisor_trap_entry

/* The existing CLINT exposes only MTIP. M-mode acknowledges/rearms it, then
 * raises delegated SSIP so the kernel's real policy stays in S-mode. */
  .globl machine_timer_trap
  .type machine_timer_trap, @function
machine_timer_trap:
  addi sp, sp, -16
  sw t0, 0(sp)
  sw t1, 4(sp)
  sw t2, 8(sp)

  csrr t0, mcause
  li t1, 0x80000007             /* machine timer interrupt */
  bne t0, t1, machine_trap_bad

  /* mtimecmp = mtime + 2000, written in the CLINT-safe high/low/high order. */
  li t0, 0x02004000
  li t1, -1
  sw t1, 4(t0)
  li t1, 0x0200bff8
  lw t2, 0(t1)
  addi t2, t2, 2000
  sw t2, 0(t0)
  sw zero, 4(t0)

  /* SSIP is software pending and delegated to the supervisor handler. */
  csrr t0, mip
  ori t0, t0, 2
  csrw mip, t0

  lw t0, 0(sp)
  lw t1, 4(sp)
  lw t2, 8(sp)
  addi sp, sp, 16
  mret

machine_trap_bad:
  li t0, 0x00100000
  li t1, 0x00013333
  sw t1, 0(t0)
1:
  wfi
  j 1b
  .size machine_timer_trap, . - machine_timer_trap
