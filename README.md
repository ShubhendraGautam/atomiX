# atomiX

A computer system built from scratch — RISC-V CPU → SoC → kernel → OS — that
grows into a **reconfigurable FPGA accelerator platform**: one FPGA serving as
CPU, TPU-style matrix engine, or other roles, managed by our own kernel
(`aXos`) and driven from a host PC through our own driver (`axhost`).

**Start here: [DESIGN.md](DESIGN.md)** — the design contract. Every closed
decision, the architecture, and the phased roadmap live there.

## Build it your way

The verified reference machine is the default, but it is not a fixed appliance.
CPU, SoC, memory, UART, CLINT, SPI, board, software, aXos scheduler, and aXos
virtual-memory implementations are selectable components. The component catalog
keeps contracts at integration seams rather than prescribing how a user must
implement their hardware or kernel.

```bash
make component-list
make config-check CONFIG=configs/sim-delayed.json
make sim CONFIG=configs/sim-bram.json \
  RAM_INIT_FILE="$PWD/sw/baremetal/build/hello.hex"
make software CONFIG=configs/sim-axos.json
make -C sw/kernel kernel-config KERNEL_CONFIG=../../configs/kernel-cooperative.json
```

See [components/README.md](components/README.md) for out-of-tree component
selection, [docs/components.md](docs/components.md) for the architecture, and
[docs/component-map.md](docs/component-map.md) for the repository-wide map.

## Principles

- **Scratch-build only what teaches** (CPU core, bus, kernel, accelerator
  roles); **adopt the industry standard everywhere else** (RISC-V ISA, stock
  GCC, ELF, riscv-tests, riscv-formal, Verilator, QEMU-`virt` memory map,
  16550 UART).
- **Simulation-first**: the whole stack runs under Verilator/QEMU/our own ISS
  before any hardware is bought. FPGA (Lattice ECP5, open Yosys+nextpnr flow)
  is the target, not the prerequisite.
- **Verified, not demoed**: golden-model cosimulation + official ISA tests +
  formal proofs define "correct".

## Getting started

```bash
git clone --recurse-submodules <repo-url> && cd atomiX
sudo apt update && sudo apt install build-essential gcc-riscv64-unknown-elf \
  picolibc-riscv64-unknown-elf verilator qemu-system-misc
make -C sim/axsim test                # build the ISS + run directed tests
make -C sim/unit run-soc-timer        # RTL SoC + CLINT interrupt integration
make -C sw/baremetal check-hello      # same C image: ISS, QEMU, and RTL
make -C sw/baremetal check-timer      # CLINT interrupts: ISS, QEMU, and RTL
make -C sw/baremetal check-preempt    # timer-preempted tasks: all three
make -C sim/unit run-privilege        # M/S/U transitions, delegation, sret
make -C sim/unit run-sv32             # Sv32 walks, A/D updates, page fault
make -C sim/unit run-axdram-model     # Phase-6 delayed-memory contract
make -C sim/unit run-axcache          # Phase-6 cache fill/hit/flush/bypass
make -C sim/unit run-axsdram           # ULX3S x16 SDRAM controller contract
make -C sim/unit run-axuart-phy        # physical 115200 UART transmitter/receiver
make -C sim/testgen paging             # 100k randomized Sv32/U-mode cosim events
make -C tests/riscv-tests/isa XLEN=32 RISCV_PREFIX=riscv64-unknown-elf- -j"$(nproc)"
tests/run-riscv-tests.sh              # official ISA suite: 41 passed expected
tests/run-riscv-tests.sh rv32si        # supervisor ISA suite: 6 passed expected
SIM=../sim/cosim/obj_dir/axcosim tests/run-riscv-tests.sh rv32si  # lock-step
# Phase-5 aXos shell + U-mode fork/wait on ISS, QEMU, and RTL
make -C sw/kernel check-boot QEMU="$HOME/.local/bin/qemu-system-riscv32"
make -C sw/baremetal check-fencei QEMU="$HOME/.local/bin/qemu-system-riscv32"
make -C sw/kernel check-memory         # 32 MiB delayed RAM + I/D-cache RTL
make -C sw/baremetal check-sd           # SPI SDHC init + sector read on RTL
make -C sw/kernel check-storage         # aXos mounts AXFS files from SD RTL
make -C sw/kernel check-storage-write   # CMD24 write, directory update, readback
make -C sw/kernel check-sdboot           # ROM SD boot through physical SDRAM RTL
make -C sw/kernel kernel-component-test QEMU=/tmp/qemu-8.2.10/build/qemu-system-riscv32
```

The ULX3S-85F hardware target, constraints, and reversible SRAM programming
procedure are in [rtl/fpga/README.md](rtl/fpga/README.md). Physical board
bring-up is deliberately the final roadmap gate while hardware is unavailable;
the explicit [bring-up checklist](docs/ulx3s-bringup.md) remains ready for it.

Full prerequisites, per-phase tool needs, and known quirks: [docs/toolchain.md](docs/toolchain.md).

## Tested host toolchain

This is the known-working baseline used for the current verification results
(recorded 2026-07-18). It is a compatibility record, not a requirement to use
these exact patch releases.

| Component | Running version | Role / note |
|---|---:|---|
| Host OS | Ubuntu 22.04.5 LTS | WSL2 host used for bring-up |
| RISC-V GCC | 10.2.0 (`gcc-riscv64-unknown-elf`) | Use `-march=rv32im`; this older parser rejects explicit `_zicsr` |
| Verilator | 4.038 | RTL simulation |
| QEMU | 8.2.10 (local `~/.local` install) | Three-platform checks; invoke with `-cpu rv32,pmp=false` for the PMP-less aXcore model. Ubuntu 22.04's packaged 6.2.0 is insufficient for Phase 5 S/U mode. |
| Yosys | 0.67+ (upstream, `45ea2b8d6`) | Formal flow; Ubuntu's bundled 0.9 is insufficient |
| nextpnr-ecp5 / Trellis `ecppack` | Not installed on the recorded host | Required only for ULX3S place-and-route; use the matched OSS CAD Suite documented below |
| openFPGALoader | Not installed on the recorded host | Required only to program a physical ULX3S; `program` is SRAM-only and reversible |
| SymbiYosys | upstream install | Formal-job launcher; see the exact setup in `docs/toolchain.md` |
| Boolector | 1.5.118 | SMT solver |
| Z3 | 4.8.12 | Alternate SMT solver |
| Python | 3.10.12 | Test generation and runners |
| GNU Make | 4.3 | Build orchestration |
| Git | 2.34.1 | Source and submodule management |

Ubuntu deliberately keeps a stable package set for an LTS release, applying
security and critical bug fixes rather than automatically tracking every
upstream release. That is why `apt install qemu-system-misc` installs 6.2 on
Ubuntu 22.04 instead of the newest QEMU. A local newer QEMU can coexist with
the distro package; the safe setup is documented in [docs/toolchain.md](docs/toolchain.md).

## Layout

| Path | Contents |
|---|---|
| [docs/](docs/) | Per-block specifications as they solidify |
| [components/](components/) | Selectable implementation manifests and extension guide |
| [configs/](configs/) | Reproducible [system profiles](configs/README.md) for simulation and boards |
| [tools/](tools/) | Dependency-free component configuration resolver |
| [rtl/](rtl/) | All synthesizable SystemVerilog: core, SoC shell, roles, board tops |
| [sim/](sim/) | ISS golden model, cosimulation harness, test generator |
| [formal/](formal/) | riscv-formal + SymbiYosys configurations |
| [sw/](sw/) | Bare-metal bring-up, the aXos kernel, userland, host-side driver |
| [tests/](tests/) | riscv-tests integration + our directed tests |

Each directory's README states its role, the standards it follows, and the
roadmap phase that brings it to life.
