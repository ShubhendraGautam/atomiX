# Dependencies and compatibility

atomiX separates its dependency tiers so a simulator-only user does not need a
formal toolchain or FPGA tools.  The commands below are safe to review and run
yourself; this repository never installs system packages automatically.

For exact installation procedures, version-specific workarounds, and commands
for a local QEMU or upstream formal stack, use [toolchain.md](toolchain.md).

## Core: simulation and target software

Required for the ISS, Verilator simulation, and bare-metal images on
Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential gcc-riscv64-unknown-elf \
  picolibc-riscv64-unknown-elf verilator qemu-system-misc git make python3
```

`gcc-riscv64-unknown-elf` includes RV32 multilib support despite its package
name.  The project defaults to `-march=rv32im -mabi=ilp32`, which works with
Ubuntu 22.04's GCC 10.2 as well as newer toolchains.

## Kernel checks: a current QEMU

The packaged Ubuntu 22.04 QEMU 6.2 is adequate for basic experiments but is
not adequate for the PMP-less S/U-mode aXos checks.  Use QEMU 7 or newer and
pass it explicitly when needed:

```bash
make -C sw/kernel check-boot QEMU=/path/to/qemu-system-riscv32
```

[toolchain.md](toolchain.md#qemu-for-axos) gives a small RISC-V-only local
build procedure that coexists with the distro package and needs no system-wide
installation.

## Formal verification

The formal flow needs a current upstream Yosys, SymbiYosys (`sby`), and a
separate riscv-formal checkout.  Boolector and Z3 are useful solver choices:

```bash
sudo apt install boolector z3
```

Ubuntu 22.04's Yosys 0.9 is too old for the SystemVerilog packages used by
this project.  Follow the guarded upstream installation in
[toolchain.md](toolchain.md#formal-verification) before running:

```bash
make -C formal check
```

## FPGA synthesis, place-and-route, and programming

A board component selects one of two open flows through its manifest, and both
ship in the prebuilt [OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build/releases).
Use it rather than compiling the FPGA stack locally: it keeps the tools matched
and avoids a long one-off build.

| Board | Flow | Tools |
|---|---|---|
| ULX3S-85F (Lattice ECP5) | `ecp5` | `yosys` (`synth_ecp5`), `nextpnr-ecp5`, `ecppack` |
| Tang Nano 20K (Gowin GW2A-18C) | `gowin` | `yosys` (`synth_gowin`), `nextpnr-himbaechel` (apicula), `gowin_pack` (apicula) |
| Tang Primer 25K Dock (Gowin GW5A-25A) | `gowin` | current `yosys` with GW5A mapping, `nextpnr-himbaechel` (apicula), `gowin_pack` (apicula) |

`make -C rtl/fpga check-tools` verifies exactly the tools the selected flow
needs, and `make -C rtl/fpga synth` is a yosys-only "does it synthesise for this
board" gate that needs no place-and-route tools installed.

`openFPGALoader` and `picocom` are only needed for physical-board work.  The
setup, tool verification, and safe SRAM-versus-flash distinction are in
[toolchain.md](toolchain.md#ecp5-fpga-tools) and
[tangprimer25k-bringup.md](tangprimer25k-bringup.md) and
[ulx3s-bringup.md](ulx3s-bringup.md).

## Recorded working baseline

The following is a compatibility record from the verified Ubuntu 22.04.5 WSL2
host on 2026-07-18, not a set of strict pins:

| Tool | Recorded version | Use |
|---|---:|---|
| RISC-V GCC | 10.2.0 | RV32 bare-metal and kernel images |
| Verilator | 4.038 | RTL simulation |
| QEMU | 8.2.10 (local) | Three-platform and aXos checks |
| Yosys | 0.67+ (upstream) | Formal flow |
| Python | 3.10.12 | Test generation and runners |
| GNU Make | 4.3 | Build orchestration |

Newer compatible releases are welcome.  Record the version and the evidence
you ran when changing a toolchain assumption.
