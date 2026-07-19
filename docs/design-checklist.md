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
- [x] A non-reference functional unit demonstrates the swap-evidence path:
  `muldiv.fast-mul` passes the identical unit testbench, directed cosim, the
  rv32um ISA suite, and randomized fuzzing through the harness unit
  overrides.  Evidence: `make -C sim/unit run-muldiv-fastmul` and
  `make -C sim/cosim test rv32um
  MULDIV_SV=../../components/muldiv/fast-mul/muldiv.sv`.

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

- [x] Role-MMIO contract: fixed 64 KiB window at `0x4000_0000` with
  `ROLE_ID`/`VERSION`/`DOORBELL`/`STATUS` header, selectable `role`
  components (`role.none` shell default, `role.loopback` proof), and a
  bare-metal driver path.  Evidence: `make -C sw/baremetal check-role` and
  `make component-test`.
- [x] First real accelerator role, TPU-lite (int8 weight-stationary systolic
  GEMM), attached behind the role window.  Evidence:
  `make -C sw/baremetal check-tpu` (verifies plain, accumulate, and ReLU GEMM
  jobs against an on-core reference and prints the role-versus-CPU cycle
  counts).
- [x] Second real accelerator role, GPU-compute (an 8-lane SIMT vector engine
  with a straight-line kernel ISA, per-lane register files, flat global memory,
  and tail-thread predication), sharing the same doorbell/descriptor driver
  model.  Evidence: `make -C sw/baremetal check-gpu` (verifies saxpy, fused
  multiply+ReLU, and a masked-tail reduction-style kernel against an on-core
  reference and prints the role-versus-CPU cycle counts).
- [x] aXos in-kernel role driver: the management kernel (not a bare-metal test
  program) discovers the role through the window device-mapped into its S-mode
  address space and drives a job end-to-end from the resident shell — the first
  piece of the shell control plane, on which the host-link service will sit.
  Evidence: `make -C sw/kernel check-role-driver` (the `role` command discovers
  and drives `role.loopback` through the RTL shell).
- [ ] Partial reconfiguration of the role region on a live bitstream —
  research staged in [partial-reconfig.md](partial-reconfig.md); no
  capability claim before its stage-4 board evidence.
- [x] Host-link control plane (base): a framed request/response protocol
  ([host-protocol.md](host-protocol.md)), an aXos host-link service that
  dispatches frames to the in-kernel role driver above, and the host-side
  `axhost` driver — a host PC discovers the role and runs a job on it over the
  link, end-to-end in simulation through the virtual-pipe (console byte-pipe)
  transport.  Evidence: `make -C sw/kernel check-hostlink`.
- [ ] Host-link enhancements: a dedicated USB-serial channel (a second
  byte-pipe peripheral, so console and host-link coexist — lands with the board
  gate); per-role job opcodes (a GEMM descriptor for TPU-lite, a SIMT kernel
  for GPU-compute) on the same frame format; and buffer/stream and asynchronous
  completion ops.
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
