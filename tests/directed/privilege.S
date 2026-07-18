# Phase 4 privilege integration regression.
#
# Exercises the architectural path M -> S (mret), delegated ECALL from S,
# S -> U (sret), delegated ECALL from U, then an illegal U-mode access to an
# M CSR.  The first three traps must land in S; the final illegal instruction
# is deliberately not delegated and must land in M.  Any wrong destination
# or saved PC/cause reports a unique failure through the test finisher.
    .section .text.init
    .globl _start
_start:
    la    x1, m_handler
    csrw  mtvec, x1
    la    x1, s_handler
    csrw  stvec, x1

    # Delegate ECALL from U (8) and S (9), but not illegal instructions.
    li    x1, 0x300
    csrw  medeleg, x1

    # Enter S at s_entry by changing MPP from reset's M to S.
    la    x1, s_entry
    csrw  mepc, x1
    li    x1, 0x1800
    csrc  mstatus, x1
    li    x1, 0x0800
    csrs  mstatus, x1
    mret

s_entry:
    ecall                         # delegated cause 9 -> s_handler
1:  j     1b                      # never resume here

u_entry:
    ecall                         # delegated cause 8 -> s_handler
2:  j     2b                      # never resume here

u_illegal:
    csrr  x5, mstatus             # illegal from U, cause 2 -> m_handler
3:  j     3b                      # never resume here

# First visit is an S-mode ECALL.  It moves the return point to u_entry,
# clears SPP, and sret therefore lowers the privilege to U.  The second visit
# is the U-mode ECALL and returns to u_illegal, retaining SPP=0.
s_handler:
    csrr  x5, sscratch
    beq   x5, x0, from_s_ecall

from_u_ecall:
    csrr  x6, scause
    li    x7, 8
    bne   x6, x7, fail1
    csrr  x6, sepc
    la    x7, u_entry
    bne   x6, x7, fail2
    la    x6, u_illegal
    csrw  sepc, x6
    sret

from_s_ecall:
    csrr  x6, scause
    li    x7, 9
    bne   x6, x7, fail3
    csrr  x6, sepc
    la    x7, s_entry
    bne   x6, x7, fail4
    li    x5, 1
    csrw  sscratch, x5
    la    x6, u_entry
    csrw  sepc, x6
    csrw  sstatus, x0             # clear SPP before sret: return to U
    sfence.vma x0, x0             # S-mode TLB invalidation is legal here
    sret

# The U-mode MCSR read must not be delegated.  Its machine trap proves that
# the core both enforced CSR privilege and restored M-mode trap state.
m_handler:
    csrr  x5, mcause
    li    x6, 2
    bne   x5, x6, fail5
    csrr  x5, mepc
    la    x6, u_illegal
    bne   x5, x6, fail6
    csrr  x5, mtval
    li    x6, 0x300022f3          # csrr x5, mstatus
    bne   x5, x6, fail7
    li    x5, 0x00100000
    li    x6, 0x5555
    sw    x6, 0(x5)
4:  j     4b

fail1: li x7, 1; j fail
fail2: li x7, 2; j fail
fail3: li x7, 3; j fail
fail4: li x7, 4; j fail
fail5: li x7, 5; j fail
fail6: li x7, 6; j fail
fail7: li x7, 7; j fail
fail:
    li    x5, 0x00100000
    slli  x7, x7, 16
    li    x6, 0x3333
    or    x7, x7, x6
    sw    x7, 0(x5)
5:  j     5b
