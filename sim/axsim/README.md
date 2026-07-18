# sim/axsim/ — the aXsim instruction-set simulator

`aXsim` is an instruction-accurate RV32IM ISS in C++ (chosen for direct
linkage into the Verilator cosim testbench).  It is the golden model the RTL
is judged against and the fast kernel-development platform.

Current scope includes RV32I execution, Zicsr and full M-mode trap state, ELF
loading with the riscv-tests `tohost`/`fromhost` harness, CLINT and UART device
models, RV32M, M/S/U trap delegation, and Sv32 page-table walks with hardware
A/D updates.  `Cpu::ext_su` (on by default) gates the privileged extension;
lock-step cosimulation compares the effective privilege at every event.

Interfaces it must expose:

- **CLI**: run an ELF, exit with the test's pass/fail code.
- **Trace mode**: one line per retired instruction (PC, raw instruction,
  rd writeback, CSR side effects, trap) — the exact record cosim compares.
- **Library API**: `step()`-able C++ object for embedding in the cosim
  testbench.

Run `make -C sim/axsim test` for directed evidence.  The wider verification
matrix is in [docs/build.md](../../docs/build.md).
