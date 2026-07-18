# Phase 4 Sv32 end-to-end regression.
#
# M-mode creates a two-level map in RAM, enables Sv32, and mret's into an
# RXU superpage. User code performs a translated RW access through an L0
# leaf, then intentionally stores into its RX-only code mapping. The M-mode
# handler checks the store-page-fault record and both relevant A/D updates.
#
# Physical memory is the existing aXcore RAM window at 0x80000000. The
# virtual code mapping starts at 0x00400000; the translated data leaf is at
# 0x00801000 -> physical 0x80006000.
    .section .text.init
    .globl _start
_start:
    la    x1, m_handler
    csrw  mtvec, x1

    # Root @ 0x80004000:
    #   VPN1=1: RXU superpage -> physical 0x80000000 (A starts clear)
    #   VPN1=2: pointer to L0 @ 0x80005000
    li    x1, 0x80004000
    li    x2, 0x2000001b          # PPN=0x80000, V|R|X|U
    sw    x2, 4(x1)
    li    x2, 0x20001401          # PPN=0x80005, V pointer
    sw    x2, 8(x1)

    # L0 @ 0x80005000: VPN0=1 -> translated user data, V|R|W|U.
    li    x1, 0x80005000
    li    x2, 0x20001817          # PPN=0x80006, A/D start clear
    sw    x2, 4(x1)

    # Sv32 root PPN 0x80004. M mode remains bare while preparing mret.
    li    x1, 0x80080004
    csrw  satp, x1
    la    x1, u_entry
    li    x2, 0x80400000          # physical -> virtual code mapping delta
    add   x1, x1, x2
    csrw  mepc, x1
    li    x1, 0x1800
    csrc  mstatus, x1             # MPP = U
    mret

u_entry:
    li    x5, 0x00801000
    li    x6, 0x0013579b
    sw    x6, 0(x5)
    lw    x7, 0(x5)
    bne   x6, x7, user_bad

    # The superpage has no W permission, so this must trap with cause 15.
    li    x5, 0x00400000
    sw    x0, 0(x5)
1:  j     1b                      # never resume after the page fault

user_bad:
    ecall                          # unexpected data value: fail in M
2:  j     2b

m_handler:
    csrr  x5, mcause
    li    x6, 15                   # store/AMO page fault
    bne   x5, x6, fail1
    csrr  x5, mtval
    li    x6, 0x00400000
    bne   x5, x6, fail2

    # Fetch's superpage PTE must have acquired A through the ibus walker.
    li    x5, 0x80004004
    lw    x5, 0(x5)
    andi  x5, x5, 0x40
    li    x6, 0x40
    bne   x5, x6, fail3

    # The L0 store mapping must have both A and D after the translated SW.
    li    x5, 0x80005004
    lw    x5, 0(x5)
    andi  x5, x5, 0xc0
    li    x6, 0xc0
    bne   x5, x6, fail4

    li    x5, 0x00100000
    li    x6, 0x5555
    sw    x6, 0(x5)
3:  j     3b

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
4:  j     4b
