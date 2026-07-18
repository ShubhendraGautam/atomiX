# rtl/roles/ — swappable accelerator roles

Mode-specific accelerators for the shell + role platform (DESIGN.md §3.3).
One subdirectory per role; each build pairs the (identical) shell with exactly
one role into a bitstream. Mode switching = flashing a different bitstream.

**Role contract** (spec: `docs/role-interface.md`, phase 7): a role is an
aXbus MMIO slave exposing an ID register, a doorbell, and a descriptor-ring
interface, plus an interrupt line — the same idiom real NVMe/GPU hardware
uses. Roles do **not** execute RISC-V; they consume descriptors that aXos
feeds them.

Planned roles:

- `loopback/` — echoes buffers back; exists purely to prove the framework
  and the host path end-to-end (phase 7).
- `tpu/` — TPU-lite: int8 systolic GEMM array on ECP5 DSP blocks,
  weight-stationary dataflow (phase 8).

A role must never require shell RTL changes; if it seems to, the role
interface spec is what gets amended.
