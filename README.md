# atomiX

A computer system built from scratch — RISC-V CPU → SoC → kernel → OS — that
grows into a **reconfigurable FPGA accelerator platform**: one FPGA serving as
CPU, TPU-style matrix engine, or other roles, managed by our own kernel
(`aXos`) and driven from a host PC through our own driver (`axhost`).

**Start here: [DESIGN.md](DESIGN.md)** — the design contract. Every closed
decision, the architecture, and the phased roadmap live there.

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
make -C tests/riscv-tests/isa XLEN=32 RISCV_PREFIX=riscv64-unknown-elf- -j"$(nproc)"
tests/run-riscv-tests.sh              # official ISA suite: 41 passed expected
```

Full prerequisites, per-phase tool needs, and known quirks: [docs/toolchain.md](docs/toolchain.md).

## Layout

| Path | Contents |
|---|---|
| [docs/](docs/) | Per-block specifications as they solidify |
| [rtl/](rtl/) | All synthesizable SystemVerilog: core, SoC shell, roles, board tops |
| [sim/](sim/) | ISS golden model, cosimulation harness, test generator |
| [formal/](formal/) | riscv-formal + SymbiYosys configurations |
| [sw/](sw/) | Bare-metal bring-up, the aXos kernel, userland, host-side driver |
| [tests/](tests/) | riscv-tests integration + our directed tests |

Each directory's README states its role, the standards it follows, and the
roadmap phase that brings it to life.
