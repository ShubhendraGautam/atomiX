# sim/axsim/ — the aXsim instruction-set simulator

**The first thing built in the project** (phase 0): an instruction-accurate
RV32IM ISS in C++ (chosen for direct linkage into the Verilator cosim
testbench). It is the golden model the RTL is judged against, and doubles as
the fast kernel-development platform (~10⁸ inst/s vs ~10⁶ cycles/s Verilated).

Scope grows with the CPU phases:

1. RV32I fetch/decode/execute, regfile, flat memory with the DESIGN.md §3.2
   map (phase 0)
2. Zicsr + full M-mode trap machinery: `mstatus`, `mtvec`, `mepc`, `mcause`,
   `mtval`, ecall/mret, illegal instruction (phase 0)
3. ELF loading + riscv-tests `tohost`/`fromhost` harness (phase 0)
4. CLINT + UART device models, interrupt injection (phase 3)
5. M extension (phase 2); S/U modes and Sv32 page-table walks (phase 4)

Interfaces it must expose:

- **CLI**: run an ELF, exit with the test's pass/fail code.
- **Trace mode**: one line per retired instruction (PC, raw instruction,
  rd writeback, CSR side effects, trap) — the exact record cosim compares.
- **Library API**: `step()`-able C++ object for embedding in the cosim
  testbench.

**Phase 0 exit criterion: passes all rv32ui + rv32mi riscv-tests.**
