# formal/ — formal verification

Glue and configurations for **riscv-formal** driven by **SymbiYosys**. Formal
checks complement simulation: they explore all instruction/data combinations
within a bounded window and prove the RVFI architectural trace satisfies the
selected ISA properties.

## Prerequisites

Install the required tools as documented in
[`docs/toolchain.md`](../docs/toolchain.md#formal-verification). In short,
the host needs `yosys`, `sby`, and an unmodified `riscv-formal` checkout at
`/opt/riscv-formal`. Boolector and Z3 are optional for exploratory jobs.

```bash
command -v sby yosys
test -d /opt/riscv-formal
```

The default suite uses Yosys's built-in SAT engine after riscv-formal has
generated the ISA assertions. It completes on a commodity developer machine
without an additional SMT solver. Boolector and Z3 remain useful for expanded
or exploratory jobs. To preserve reproducibility, do not vendor or edit the
`/opt/riscv-formal` checkout from this repository.

## Contents

- `components/core/pipeline5/axcore_rvfi_wrapper.sv` connects aXcore's
  one-retire-per-cycle RVFI trace to a formal-only data bus and a stable,
  arbitrary instruction word. This initial bounded suite proves instruction
  semantics across all operand/data values without requiring an impractically
  expensive arbitrary-program history on a developer workstation.
- `checks.cfg` selects the RV32I configuration and bounded check depths. The
  product configuration enables RV32M; its fixed-latency unit is covered by
  directed, randomized, and official RV32M lock-step ISA tests.
- Generated SymbiYosys jobs and SAT artifacts are placed in `build/` and are
  ignored by Git.

## Run

```bash
make -C formal check          # generate and run the default proof suite
make -C formal list           # list generated checks
make -C formal clean          # remove generated proof artifacts
```

Use an individual generated check when iterating on a failure, for example:

```bash
make -C formal generate
python3 formal/run_checks.py insn_add_ch0
```

The generated check and its solver log are below `formal/build/sat/`.

Recommended gate policy: run formal jobs for changes touching
`components/core/pipeline5/` and run the relevant simulation legs for every
behavioral change.  The project-wide command matrix is in
[docs/build.md](../docs/build.md).
