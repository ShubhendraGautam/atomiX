# sim/cosim/ — lock-step cosimulation harness

The strongest verification leg (phase 1): a **Verilator** testbench that runs
the RTL core and `aXsim` in lock step. On every retired instruction it
compares:

- PC and raw instruction bits
- register writeback (rd, value)
- CSR side effects
- trap taken / cause

On divergence it stops at the first mismatching field and prints the event
number plus RTL/ISS values. Re-run the same program with `--trace` for a
one-line-per-event trace. A cosim failure is always one of: RTL bug, ISS bug,
or spec misreading — all three are wins.

`tb_cosim.cpp` is the runnable harness. It owns an RTL memory image and links
the ISS with a separate image, then compares every commit event and the
post-commit M-mode CSR state. This catches both datapath/control divergence
and accidental shared-state masking.

```bash
make -C sim/cosim test                 # directed programs, 0 and N wait states
make -C sim/cosim riscv-tests          # built rv32ui + rv32mi binaries
./sim/cosim/obj_dir/axcosim --bin PROGRAM --trace
```

Inputs grow to include riscv-tests binaries, directed tests from `tests/`, and
random streams from `sim/testgen/`. Phase 1 exit bar remains 10⁷ random
instructions with zero divergence. CI wiring is the next verification task.
