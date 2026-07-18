# Regression: timer interrupt landing on the exact commit cycle of an mret
# executed with mstatus.MIE = 1 (the nested-interrupt pattern). The mret
# still retires: mepc must name its return target (not mret_pc+4) and the
# trap's MPIE must save the mret-restored MIE. The original RTL suppressed
# the mret entirely, losing the return address and the MIE restore.
# Exits through the test finisher: 0x5555 pass, 0x3333|n<<16 fail n.
    .section .text.init
    .globl _start
_start:
    la    x1, handler
    csrw  mtvec, x1
    li    x1, 0x80              # MTIE
    csrw  mie, x1
    la    x2, ret_target
    csrw  mepc, x2
    li    x1, 0x88              # MPIE = 1, MIE = 1: mret commits with MIE set
    csrw  mstatus, x1
    mret                        # marker: the collision commit
ret_target:
    j     fail1                 # reached only if the interrupt never fires

handler:
    csrr  x10, mcause
    li    x11, 0x80000007       # machine timer interrupt
    bne   x10, x11, fail2
    csrr  x10, mepc
    la    x11, ret_target       # the bug: this used to read mret_pc+4
    bne   x10, x11, fail3
    csrr  x10, mstatus
    andi  x10, x10, 0x88
    li    x11, 0x80             # MPIE = mret-restored MIE = 1, MIE = 0
    bne   x10, x11, fail4
    li    x5, 0x00100000
    li    x6, 0x5555
    sw    x6, 0(x5)
1:  j     1b

fail1: li x7, 1; j fail
fail2: li x7, 2; j fail
fail3: li x7, 3; j fail
fail4: li x7, 4; j fail
fail:
    li    x5, 0x00100000
    slli  x7, x7, 16
    li    x6, 0x3333
    or    x7, x7, x6
    sw    x7, 0(x5)
1:  j     1b
