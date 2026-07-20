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
- [x] A scalable core family demonstrates the seam at performance granularity:
  `core.ax2-{s,m,l}` is a dual-issue in-order superscalar RV32IM machine-mode
  core (block-RAM instruction cache, bundle BTB, 4R2W register file) sharing the
  reference core's decoder/immdec/branch-comparator.  Tiers differ only in issue
  width, cache size, and BTB depth.  Evidence: `make -C sim/unit run-suite-ax2`
  (every tier against the official rv32ui + rv32um binaries on the RTL — 49
  tests × 3 tiers × 3 wait-state settings — plus the directed programs) and
  `make -C sw/baremetal check-suite-ax2` (SoC integration: interrupts, fence.i,
  IPC, and the gpu1 role).  Measured 2.53× core.minimal and 1.60× core.pipeline5
  on the mixed workload; see [hardware-capabilities.md](hardware-capabilities.md).
  It implements machine mode with physical addressing only — no Sv32/S/U — and
  has no RVFI surface, so it does not carry the reference core's cosim or
  riscv-formal evidence.
- [x] A whole-CPU swap demonstrates the same seam at core granularity:
  `core.minimal` is a compact multi-cycle RV32IM machine-mode core (no MMU/S/U,
  reusing the reference decoder/ALU/mul-div/regfile) built as an accelerator
  host.  Evidence: `make -C sw/baremetal check-suite-minimal` — one suite that
  runs `core.minimal` driving the CPU (hello), the GPU role, and the TPU role.
  It ships in the `tangnano20k-gpu` and `ulx3s-85f-gpu` profiles (minimal host +
  GPU).  It does not yet carry the reference core's cosim/riscv-formal evidence,
  so architectural equivalence at ISA granularity is still open.

- [x] Memory-system components sized and shaped for real workloads:
  `cache.writeback` (direct-mapped, write-back, write-allocate, drain-on-flush)
  and `muldiv.radix4` (single-cycle multiply, 16-cycle divide), plus cache
  geometry exposed as the `cache_lines` / `cache_words_per_line` profile
  settings — the stock 256-byte cache was a composition smoke size, not a
  working one.  Evidence: `make -C sim/unit run-muldiv-radix4` (the same
  latency-agnostic unit testbench the reference divider passes),
  `make -C sw/baremetal check-suite-ax2`, and `python3 tools/bench.py render`
  (2.91× on a renderer-shaped workload, of which the write-back policy is
  1.55×).  `cache.writeback` carries a documented constraint: it must not be
  paired with a core whose fetch port writes memory (the Sv32 walker).
- [x] Tunable components rather than near-duplicate variants: a component is
  the unit of *architecture*, and a size within it is a build-time parameter.
  `core.ax2` and `role.gpu1` are each one component; `role.gpu-compute` absorbed
  its lane variants the same way.  Parameters are declared in the manifest with
  the defaults that define the baseline, overridden per profile by name, and
  validated — an undeclared parameter is a configuration error naming what the
  component does declare.  This replaced eleven components with three.  Evidence:
  `make config-check-all` and the parameter sweeps in
  `make -C sim/unit run-suite-ax2` / `run-suite-gpu1`.

## Userspace ABI (next milestone)

aXos has a scheduler, an allocator, a filesystem, and a shell, but no way to
*run a program*: there is no syscall ABI, no loader, and no C library.  Nothing
compiled from C can target it today, which is the gap between "the CPU can run a
real program" and "the system can host one".  Two findings from the render
benchmark make the gap concrete: the bare-metal link has no libgcc (so a 64-bit
divide is an undefined `__udivdi3`), and `link.ld` hardcodes a 128 KiB image.

Staged so each step has its own evidence rather than landing as one large jump:

- [ ] **Syscall ABI.** Define and document the M/U boundary: the `ecall`
  convention (which register carries the number, which carry arguments, how a
  negative return encodes an error), the initial set — `exit`, `write`, `read`,
  `open`, `close`, `lseek`, `brk`/`sbrk`, `fstat` — and the guarantees each
  makes.  This is a contract document plus a kernel trap handler, testable with
  a hand-written assembly program before any libc exists.
- [ ] **Program loader.** Load a flat or ELF image into a user address space and
  enter U-mode with a defined initial stack, argv/envp layout, and entry
  contract.  Needs the reference core's S/U modes, so it pins the pairing:
  `core.pipeline5` for the hosted profile, not `core.ax2`.
- [ ] **C library.** Retarget a small libc (newlib or picolibc) onto the
  syscalls, with `malloc` over `brk` backed by `allocator.free-list` and file
  I/O backed by `filesystem.axfs`.  Link libgcc so 64-bit arithmetic works.
- [ ] **Evidence.** A compiled, unmodified C program — one that allocates,
  reads a file, and prints — runs on aXos through the loader.  Then raise the
  image ceiling and run something substantial enough to be a real test of the
  ABI rather than a demonstration of it.

Open questions to settle first: whether the ABI targets a subset of the Linux
RISC-V calling convention (portable, familiar, larger) or a deliberately small
atomiX-specific one (smaller, needs its own libc port); and whether the loader
takes ELF directly or a pre-flattened image.

## Change-ready checklist

Use this for a substantive implementation or interface change:

- [ ] Update the component manifest and profile validation if source selection
  changes.
- [ ] Update the architecture/contract document at the affected boundary.
- [ ] Run the narrow unit or simulator test, then the relevant composition
  check; run formal after core/RVFI changes.
- [ ] Record any new tool, timing, capacity, or hardware assumption in
  [dependencies.md](dependencies.md) or the appropriate board guide.
- [ ] Update [workflow.md](workflow.md) when a milestone adds or changes a
  build, test, or deploy command, or a build knob or profile users run.
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
- [x] Scalable accelerator role family, `role.gpu1-{s,m,l,xl}`: the SIMT engine
  rebuilt around **banked global memory** (NBANKS interleaved block RAMs behind
  a lane→bank crossbar with round-based conflict serialisation) and a real
  control ISA (structured IF/ELSE/ENDIF divergence, uniform and any-lane
  branches, compare-set, integer divide, cross-lane shuffle, displaced
  addressing).  Banking is what makes lane count worth scaling: the previous
  single-port engine gained only 1.18× going from 8 to 16 lanes, where gpu1
  gains 1.69–1.82× per doubling and is 2.70× the old engine at equal lane count.
  Geometry is published in a CAPS register, so one driver and one oracle serve
  every tier.  Evidence: `make -C sim/unit run-suite-gpu1` (all four tiers
  against a C++ interpreter of the ISA, including the maximal-bank-conflict and
  worst-case-serialisation kernels that pin the store-ordering invariant) and
  `make -C sw/baremetal check-gpu1` (the same battery driven on-core through the
  shell window).
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
- [x] Per-role host-link job opcodes: `TPU_GEMM` (int8 systolic GEMM) and
  `GPU_RUN` (an uploaded SIMT kernel over a flat data buffer) on the same frame
  format, backed by in-kernel TPU-lite and GPU-compute drivers.  A host PC drives
  all three real accelerators over the link, each checked against a host-side
  reference.  Evidence: `make -C sw/kernel check-hostlink` (loopback, TPU-lite,
  and GPU-compute profiles).
- [ ] Remaining host-link enhancements: a dedicated USB-serial channel (a
  second byte-pipe peripheral, so console and host-link coexist — lands with the
  board gate); buffer/stream and asynchronous-completion ops; and bitstream-
  upload mode switching.
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
- [~] Tang Nano 20K (Gowin GW2A-18C) board component, constraints, and Gowin
  flow exist; the design synthesises and fits.  Evidence:
  `make -C rtl/fpga synth COMPONENT_CONFIG=$PWD/configs/tangnano20k.json`
  produces a Yosys netlist in which the 32 KB main memory maps to 32 `DPB`
  block-RAM cells (not flip-flops) — the BRAM-only bring-up needs registered
  reads (`axram` `SYNC_READ=1`), verified functionally by `make -C sim/soc run
  CONFIG=configs/sim-bram.json SYNC_READ=1` (hello prints, one wait state per
  access).  Fit on the GW2A-18C: 32 DPB, ~2.7k FF, ~11k LUT4.  P&R and
  bitstream await the apicula tools (`nextpnr-himbaechel`, `gowin_pack`).
- [~] Attach an accelerator role on the Tang Nano.  The parameterized SIMT
  engine (gpu_engine.sv, `NLANES`) fits: the shipped `configs/tangnano20k-gpu.json`
  (minimal host + 6-lane) synthesises to ~20.2k LUT4, 32 DPB, 6 DSP — inside the
  GW2A-18C at 97% (tight); `role.gpu-compute` at 4 lanes fits comfortably
  (~18.9k).  Functional equivalence to the 8-lane reference is checked by
  `make -C sw/baremetal check-gpu`, and throughput by `check-gpu-perf` (poly
  kernel ~12.9× vs on-core).  Per-hardware fit:
  [hardware-capabilities.md](hardware-capabilities.md); still-open TPU/all-three
  cases: [tangnano-capacity.md](tangnano-capacity.md).
- [ ] Run ECP5 / Gowin place-and-route, generate the bitstream, and record
  timing and resource reports.
- [ ] Program SRAM only for the first board test; confirm serial output and
  reset/reload behavior.
- [ ] Validate external SDRAM and SD read/write persistence on the physical
  board.
- [ ] Decide separately, and only after the SRAM path is proven, whether a
  persistent flash operation is appropriate.

The detailed, safe board procedure is
[ulx3s-bringup.md](ulx3s-bringup.md).
