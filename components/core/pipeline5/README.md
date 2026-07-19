# core.pipeline5 — aXcore CPU

This directory owns the complete reference CPU implementation: synthesizable
RTL plus the `axcore_rvfi_wrapper.sv` formal-only adapter. `rtl/core/` is now
only a short architecture-facing signpost; build selection always comes from
this component's manifest.

The from-scratch RISC-V CPU: **RV32IM + Zicsr**,
classic 5-stage pipeline (IF ID EX MEM WB), M/S/U modes, and Sv32 MMU.  See
DESIGN.md §4 for the closed microarchitecture.

Load-bearing properties (do not regress):

- **Two aXbus masters** — `ibus` (fetch) and `dbus` (load/store); Harvard at
  the core edge so caches later attach per-port with no core changes.
- **Precise exceptions**: PC + exception tag travel with every instruction to
  a single commit point in WB; interrupts inject there too.
- **Serialized irregular instructions**: CSR writes, `mret`, `fence.i` flush
  younger instructions and execute alone — no CSR forwarding network exists.
  RV32M operations also execute alone through the selected `muldiv` component
  (default: the fixed-32-cycle iterative `muldiv.iterative32`), then resume
  fetch at the following instruction.
- **Register file**: 32×32 flip-flops, x0 hardwired, 2R1W, internal
  write-before-read bypass.

Structure: `axcore.sv` is the pipeline — the five stages and the hazard/
forwarding/flush control live there, clearly sectioned, because the pipeline
registers are the boundaries that matter and module ports between stages
would only add noise. Functional units are separate, individually-tested
modules. Four of them are selectable components in their own right, pulled in
through this manifest's `defaults` and swappable per profile: the `alu`,
`muldiv`, `regfile`, and `mmu` kinds (`components/alu/single-cycle/`,
`components/muldiv/iterative32/`, `components/regfile/flipflop/`,
`components/mmu/sv32/`). The units entangled with pipeline and privilege
semantics stay here: `axcore_pkg.sv` (shared types — including `alu_op_t`,
part of the ALU contract), `immdec.sv`, `decoder.sv`, `branch_cmp.sv`, and
`csr_file.sv`. The module port list of each unit is its contract; `muldiv`'s
`start/busy/done` handshake tolerates any latency, so an alternative
implementation needs no core change.

Correctness bar (DESIGN.md §4.3): riscv-tests **and** lock-step ISS cosim
**and** riscv-formal — all three.  Run the current evidence commands from
[docs/build.md](../../../docs/build.md).

`axcore.sv` also exposes a non-invasive `trace_*` commit-observation port.
It is sampled by `sim/cosim/` immediately before the committing clock edge;
it has no control-path role and is retained so verification need not reach
into Verilator-generated internals.
