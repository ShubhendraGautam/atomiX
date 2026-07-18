# rtl/core/ — aXcore CPU

The from-scratch RISC-V CPU: **RV32I + Zicsr** (M extension in phase 2),
classic 5-stage pipeline (IF ID EX MEM WB), machine mode first, S/U modes +
Sv32 MMU in phase 4. See DESIGN.md §4 for the closed microarchitecture.

Load-bearing properties (do not regress):

- **Two aXbus masters** — `ibus` (fetch) and `dbus` (load/store); Harvard at
  the core edge so caches later attach per-port with no core changes.
- **Precise exceptions**: PC + exception tag travel with every instruction to
  a single commit point in WB; interrupts inject there too.
- **Serialized irregular instructions**: CSR writes, `mret`, `fence.i` flush
  younger instructions and execute alone — no CSR forwarding network exists.
- **Register file**: 32×32 flip-flops, x0 hardwired, 2R1W, internal
  write-before-read bypass.

Structure: `axcore.sv` is the pipeline — the five stages and the hazard/
forwarding/flush control live there, clearly sectioned, because the pipeline
registers are the boundaries that matter and module ports between stages
would only add noise. Functional units are separate, individually-tested
modules: `axcore_pkg.sv` (shared types), `regfile.sv`, `alu.sv`, `immdec.sv`,
`decoder.sv`, `branch_cmp.sv`, `csr_file.sv`; later `mmu.sv`/`tlb.sv`.

Correctness bar (DESIGN.md §4.3): riscv-tests **and** lock-step ISS cosim
**and** riscv-formal — all three. Built in phase 1, after the ISS exists.

`axcore.sv` also exposes a non-invasive `trace_*` commit-observation port.
It is sampled by `sim/cosim/` immediately before the committing clock edge;
it has no control-path role and is retained so verification need not reach
into Verilator-generated internals.
