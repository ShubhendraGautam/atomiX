# Engineering checklist and evidence

This is the live completion checklist for atomiX.  It tracks evidence, not
just code presence: a checked item has a reproducible command or a recorded
physical observation behind it.

Status legend:

- `[x]` Verified by the listed automated evidence.
- `[~]` Implemented and simulation/synthesis tested; physical-board evidence is pending.
- `[ ]` Planned or intentionally deferred.

The architectural contract remains [DESIGN.md](../DESIGN.md); component
contracts and selections are in [components/](../components/).

## Reference computer

- [x] RV32IM five-stage reference core with Zicsr, M/S/U privilege modes, and
  Sv32 translation.  Evidence: `make -C sim/unit test`,
  `make -C sim/cosim test`, and `make -C formal check`.
- [x] Golden ISS, Verilator lock-step harness, official ISA-suite integration,
  and randomized instruction/paging generation.  Evidence:
  `make -C sim/axsim test`, `make -C sim/testgen fuzz`, and
  `make -C sim/testgen paging`.
- [x] aXbus reference interconnect, UART, CLINT, boot ROM, finisher, BRAM,
  delayed memory, reference cache, SDRAM, and SPI/SD paths compose through
  checked-in profiles.  Evidence: `make component-test`.
- [x] Bare-metal image runs on ISS, QEMU, and RTL.  Evidence:
  `make -C sw/baremetal check-hello check-timer check-preempt`.
- [x] aXos boots through the selectable scheduler, VM, storage, and SD-boot
  services.  Evidence: `make -C sw/kernel kernel-component-test
  QEMU=/path/to/qemu-system-riscv32`.

## Component discipline

- [x] Selectable source implementations live under `components/`; `rtl/`
  contains generic synthesis flow and architecture signposts rather than a
  second source tree.
- [x] Profiles validate their chosen components.  Evidence:
  `make config-check-all`.
- [x] Stock component seams remain deliberately lenient so an out-of-tree
  implementation can replace a CPU, memory, peripheral, board, harness, or
  aXos service without copying the reference implementation.
- [ ] Every non-reference component must provide its own compatibility claim
  and verification evidence; selection alone never grants reference-machine
  verification status.

## Change-ready checklist

Use this for a substantive implementation or interface change:

- [ ] Update the component manifest and profile validation if source selection
  changes.
- [ ] Update the architecture/contract document at the affected boundary.
- [ ] Run the narrow unit or simulator test, then the relevant composition
  check; run formal after core/RVFI changes.
- [ ] Record any new tool, timing, capacity, or hardware assumption in
  [dependencies.md](dependencies.md) or the appropriate board guide.
- [ ] Keep physical claims separate from simulation and synthesis claims.

## Platform expansion

- [ ] Define and implement the host-link framing protocol and `axhost`.
- [ ] Define the role-MMIO/descriptor contract, then implement the first
  accelerator role (TPU-lite).
- [ ] Add PLIC/role interrupt integration when a second interrupt source
  exists.
- [ ] Evaluate A or C ISA extensions only when their enabling need is explicit;
  neither is required for the current single-hart reference machine.

## Final physical ULX3S gate

Hardware availability is intentionally not a blocker for the simulation and
component work above.  It is the final platform-evidence gate.

- [~] ULX3S-85F board component, constraints, SDRAM/UART RTL, and synthesis
  preflight exist.  Evidence: `make fpga CONFIG=configs/ulx3s-85f.json` with
  the matched OSS CAD Suite environment.
- [ ] Run ECP5 place-and-route, generate the bitstream, and record timing and
  resource reports.
- [ ] Program SRAM only for the first board test; confirm serial output and
  reset/reload behavior.
- [ ] Validate external SDRAM and SD read/write persistence on the physical
  board.
- [ ] Decide separately, and only after the SRAM path is proven, whether a
  persistent flash operation is appropriate.

The detailed, safe board procedure is
[ulx3s-bringup.md](ulx3s-bringup.md).
