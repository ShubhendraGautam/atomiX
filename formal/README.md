# formal/ — formal verification

Glue and configurations for **riscv-formal** (the industry-standard RISC-V
formal framework) driven by **SymbiYosys**, providing bounded proofs that no
amount of simulation gives: for *all* instruction sequences up to depth N,
register writeback is correct, PC ordering holds, and traps are precise.

Contents (phase 2):

- riscv-formal checker configuration for aXcore
- The RVFI (RISC-V Formal Interface) wrapper — aXcore exposes an RVFI port
  from phase 1 so this slots in without core surgery
- SymbiYosys `.sby` job files, one per property group

CI policy: formal jobs run on every change touching `rtl/core/`; simulation
legs (`sim/`) run on everything.
