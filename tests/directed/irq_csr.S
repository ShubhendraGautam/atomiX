# Regression: timer interrupt landing on the exact commit cycle of a CSR
# write. The harness (sim/unit/tb_irq_collide) asserts irq_timer precisely
# when `csrw mscratch` commits. The instruction still retires, so its write
# must land AND the trap must name its successor in mepc — the original RTL
# dropped the write because trap entry preempted it in the csr_file update
# chain. Exits through the test finisher: 0x5555 pass, 0x3333|n<<16 fail n.
    .section .text.init
    .globl _start
_start:
    la    x1, handler
    csrw  mtvec, x1
    li    x1, 0x80              # MTIE
    csrw  mie, x1
    csrsi mstatus, 0x8          # MIE = 1
    li    x2, 0x123
    csrw  mscratch, x2          # marker: the collision commit
resume:
    j     fail1                 # reached only if the interrupt never fires

handler:
    csrr  x10, mcause
    li    x11, 0x80000007       # machine timer interrupt
    bne   x10, x11, fail2
    csrr  x10, mepc
    la    x11, resume           # successor of the interrupted csrw
    bne   x10, x11, fail3
    csrr  x10, mscratch         # the bug: this write used to be dropped
    li    x11, 0x123
    bne   x10, x11, fail4
    csrr  x10, mstatus
    andi  x10, x10, 0x88
    li    x11, 0x80             # MPIE = 1 (MIE was set), MIE = 0
    bne   x10, x11, fail5
    li    x5, 0x00100000
    li    x6, 0x5555
    sw    x6, 0(x5)
1:  j     1b

fail1: li x7, 1; j fail
fail2: li x7, 2; j fail
fail3: li x7, 3; j fail
fail4: li x7, 4; j fail
fail5: li x7, 5; j fail
fail:
    li    x5, 0x00100000
    slli  x7, x7, 16
    li    x6, 0x3333
    or    x7, x7, x6
    sw    x7, 0(x5)
1:  j     1b
