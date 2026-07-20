# atomiX build, test & deploy — operational reference

**This is the single, canonical command reference for the project.** It covers
building, the full test surface, and real hardware deployment.  It is
maintained: whenever a milestone adds or changes a build, test, or deploy
command, this file is updated in the same change (see
[design-checklist.md](design-checklist.md) → change-ready checklist).

Architecture lives in [DESIGN.md](../DESIGN.md); component contracts in
[components/README.md](../components/README.md); host setup and tool quirks in
[dependencies.md](dependencies.md) and [toolchain.md](toolchain.md).  This doc
is *what to run*, not *why*.

All commands are run from the repository root unless a `-C <dir>` says otherwise.

---

## 0. Prerequisites (tiers)

Install only the tier you need; details in [dependencies.md](dependencies.md).

| Tier | Tools | Unlocks |
|---|---|---|
| Core | `riscv64-unknown-elf-gcc` (rv32 multilib), Verilator, Python 3, GNU make | build + simulation + component tests |
| Kernel | `qemu-system-riscv32` **≥ 7** | aXos S/U-mode boot checks |
| Formal | current Yosys, SymbiYosys, riscv-formal | `make -C formal check` |
| FPGA | OSS CAD Suite (Yosys, board flow tools, openFPGALoader) | synthesis + board deploy |

The board component selects the flow: ULX3S uses ECP5 (`nextpnr-ecp5`, `ecppack`),
Tang Nano 20K uses Gowin (`nextpnr-himbaechel`, `gowin_pack`).  Both ship in the
OSS CAD Suite; `make -C rtl/fpga check-tools` verifies the ones the selected
board needs.

Pass a non-default QEMU as `QEMU=/abs/path/to/qemu-system-riscv32`.  Load the
FPGA environment once per shell: `source "$HOME/opt/oss-cad-suite/environment"`.

---

## 1. The pipeline at a glance

```
 profile ─▶ build ─▶ test ───────────────▶ (synth ─▶ deploy)
 configs/   images    ISS · cosim · RTL       ECP5     ULX3S
            & ISS      roles · kernel · host   bitstream board
```

| Stage | Entry command | Proves |
|---|---|---|
| Profile | `make config-check-all` | every profile resolves to compatible components |
| Build | `make -C sim/axsim test` · `make -C sw/baremetal images` | golden ISS + a target image |
| Test | `make component-test` (+ the suites in §3) | selected components compose and run |
| Synth | `make fpga CONFIG=configs/ulx3s-85f.json` | the shell places and routes on ECP5 |
| Deploy | `make -C rtl/fpga program` | the bitstream runs on a real board (reversible) |

---

## 2. Build

### Choose / inspect a profile
```bash
make component-list                              # catalog of selectable components
make component-show COMPONENT=role.gpu-compute   # one manifest
make config-check   CONFIG=configs/sim-bram.json # resolve one profile
make config-check-all                            # resolve every profile in configs/
```

### Build the pieces
```bash
make -C sim/axsim axsim         # the golden ISS binary
make -C sw/baremetal images     # bare-metal .elf/.bin/.hex (hello, timer, role, tpu, gpu, ...)
make -C sw/kernel   images      # aXos image (build/axos_boot.{elf,bin,hex})
```

aXos build knobs (append to the `sw/kernel` command):

| Knob | Effect |
|---|---|
| `HOSTLINK=1` | host-managed personality: the console pipe carries the host-link protocol instead of the interactive shell |
| `STORAGE=1` | mount the AXFS SD image path |
| `KERNEL_CONFIG=configs/kernel-cooperative.json` | select an alternate kernel-service profile |

### Run one image on a selected SoC profile
```bash
make sim CONFIG=configs/sim-bram.json \
  RAM_INIT_FILE="$PWD/sw/baremetal/build/hello.hex"

make software CONFIG=configs/sim-axos.json   # build + run the profile's software component
```

---

## 3. Test

Run the narrowest check that covers a change, then the composition suite before
declaring a component or profile ready.

### 3.1 Core / fast
```bash
make -C sim/axsim  test        # ISS against the rv32 ISA suite
make -C sim/unit   test        # directed RTL unit benches (see `run-*` targets for one bench)
make -C sim/cosim  test        # Verilator lock-step cosimulation vs the ISS
```

### 3.2 Bare-metal, three platforms (ISS · QEMU · RTL)
```bash
make -C sw/baremetal check-hello check-timer check-preempt check-fencei
make -C sw/baremetal check-spi check-sd            # RTL-only (SPI-SD path)
```

### 3.3 Accelerator roles (RTL-only — the ISS does not model the role window)
```bash
make -C sw/baremetal check-role     # role.loopback contract proof
make -C sw/baremetal check-tpu      # TPU-lite int8 systolic GEMM vs on-core reference
make -C sw/baremetal check-gpu      # GPU-compute SIMT engine vs on-core reference (8-lane)
make -C sw/baremetal check-gpu-perf # GPU throughput regression vs on-core (8-lane)
make -C sw/baremetal check-gpu-perf-lite  # same, on the 4-lane small-FPGA role
```
The GPU engine is parameterized by lane count (gpu_engine.sv `NLANES`):
`role.gpu-compute` is the 8-lane reference, `role.gpu-compute-lite` the 4-lane
variant that fits the Tang Nano 20K (see [tangnano-capacity.md](tangnano-capacity.md)).

### 3.4 Components / composition
```bash
make config-check-all              # all profiles resolve
make component-test                # runs the supplied composition matrix (slower)
```

### 3.5 Kernel (aXos) — needs `qemu-system-riscv32` ≥ 7
```bash
make -C sw/kernel check-boot QEMU=/path/to/qemu-system-riscv32   # shell + fork/wait on ISS, QEMU, RTL
make -C sw/kernel kernel-component-test QEMU=/path/to/...        # default + cooperative scheduler
make -C sw/kernel check-memory          # 32 MiB cached external-memory RTL
make -C sw/kernel check-storage         # AXFS mount over SPI-SD (RTL)
make -C sw/kernel check-storage-write   # AXFS write/readback (RTL)
make -C sw/kernel check-sdboot          # boot ROM + SD boot through physical-SDRAM RTL
```

### 3.6 Shell control plane + host-link (RTL-only)
```bash
make -C sw/kernel check-role-driver     # aXos drives role.loopback from its own shell
make -C sw/kernel check-hostlink        # axhost drives loopback, TPU-lite, and GPU-compute over the link
```

### 3.7 Randomized + formal (run on core / RVFI / translation changes)
```bash
make -C sim/testgen fuzz           # long randomized instruction lock-step
make -C sim/testgen paging         # randomized Sv32 paging
make -C formal check               # riscv-formal bounded proofs (needs the formal tier)
```

### 3.8 Recommended full regression
```bash
make config-check-all
make -C sim/axsim test
make -C sim/cosim test
make -C sw/baremetal images
make -C sw/baremetal check-hello check-timer check-preempt check-fencei check-role check-tpu check-gpu
make component-test
make -C sw/kernel kernel-component-test QEMU=/path/to/qemu-system-riscv32
make -C sw/kernel check-role-driver check-hostlink
make -C formal check          # after core/RVFI changes
```

---

## 4. Deploy (FPGA synthesis → physical board)

Physical deployment is the **final evidence gate**.  Simulation passing is not
board proof.  The board component selects the flow; two boards are supported:

| Board | Profile | Flow | Main memory |
|---|---|---|---|
| ULX3S-85F (Lattice ECP5) | `configs/ulx3s-85f.json` | ECP5 | external SDRAM + fabric ROM |
| Tang Nano 20K (Gowin GW2A-18C) | `configs/tangnano20k.json` | Gowin | 32 KB on-chip block RAM (BSRAM) |

What each board can actually run, per configuration, backed by real synth/sim
runs: [hardware-capabilities.md](hardware-capabilities.md).  Full ULX3S
procedure and safety notes: [ulx3s-bringup.md](ulx3s-bringup.md).

### 4.1 Tool check
```bash
source "$HOME/opt/oss-cad-suite/environment"
make -C rtl/fpga check-tools  COMPONENT_CONFIG=$PWD/configs/tangnano20k.json  # flow-specific tools
make -C rtl/fpga toolchain-report COMPONENT_CONFIG=$PWD/configs/tangnano20k.json
```

### 4.2 Synthesis-only gate (no P&R tools needed)
```bash
make -C rtl/fpga synth COMPONENT_CONFIG=$PWD/configs/tangnano20k.json   # yosys netlist only
```
`synth` is the "does the design map for this board" check: it runs Yosys alone,
so it passes with only `yosys` installed.  For the Tang Nano it must map the
32 KB RAM to block RAM (`DPB` cells), not flip-flops — the memory uses
registered reads (`axram` `SYNC_READ=1`) precisely so it infers BSRAM.

### 4.3 Synthesis, place-and-route, bitstream
```bash
make fpga CONFIG=configs/ulx3s-85f.json     # top-level wrapper (ECP5), or:
make fpga CONFIG=configs/tangnano20k.json   # Gowin/Tang Nano
make -C rtl/fpga config COMPONENT_CONFIG=$PWD/configs/tangnano20k.json  # print resolved selection
```
The P&R tool (`nextpnr-ecp5` / `nextpnr-himbaechel`) prints utilisation and
timing at the end; the board clock constraint (25 MHz ULX3S, 27 MHz Tang Nano)
must pass.  Do not program a bitstream from a failed or unconstrained P&R run.

### 4.4 Program the board
```bash
make -C rtl/fpga program COMPONENT_CONFIG=$PWD/configs/tangnano20k.json  # reversible SRAM config
```
`program` targets the board named in the manifest (`ulx3s` / `tangnano20k`).
Then open the console (`picocom -b 115200 /dev/ttyUSB0`) and confirm the UART
transcript; for the Tang Nano the BL616 exposes the USB serial and LED5 shows a
~0.5 s heartbeat.

### 4.5 Persistent flash — only after a passing board proof
```bash
make -C rtl/fpga flash COMPONENT_CONFIG=$PWD/configs/tangnano20k.json  # writes config flash
```
`program` is the normal dev path; flash is persistent.

---

## 5. Maintaining this document

After every milestone, update this file in the same change if the milestone:

- adds or renames a `check-*`, build, or deploy target;
- introduces a new build knob (like `HOSTLINK=1`) or profile that users run;
- changes a required tool or version.

Keep the command groups and the §3.8 full-regression sequence current.  A
milestone is not done until its reproducible command lives here.
