# sim/cosim/ — lock-step cosimulation harness

The strongest verification leg (phase 1): a **Verilator** testbench that runs
the RTL core and `aXsim` in lock step. On every retired instruction it
compares:

- PC and raw instruction bits
- register writeback (rd, value)
- CSR side effects
- trap taken / cause

On divergence it stops, dumps an FST waveform plus the ISS trace around the
failing instruction, and reports the first mismatching field. A cosim failure
is always one of: RTL bug, ISS bug, or spec misreading — all three are wins.

Inputs: riscv-tests binaries, directed tests from `tests/`, and random
streams from `sim/testgen/`. Phase 1 exit bar: 10⁷ random instructions with
zero divergence. Runs in CI on every change.
