# Toolchain and host setup

Everything needed to build and test atomiX, captured from the actual
bring-up on Ubuntu 22.04 (jammy) under WSL2. All tools are stock distro
packages — no custom toolchain builds.

## Install (Ubuntu/Debian)

```bash
sudo apt update          # do this first: stale lists cause 404s mid-install
sudo apt install \
  build-essential \
  gcc-riscv64-unknown-elf \
  picolibc-riscv64-unknown-elf \
  verilator \
  qemu-system-misc
```

| Package | Provides | Needed from |
|---|---|---|
| `build-essential` | host `g++`, `make` — builds aXsim and the cosim harness (`clang++` works too; the Makefile auto-detects) | phase 0 |
| `gcc-riscv64-unknown-elf` | the RISC-V cross-compiler (multilib: targets rv32 despite the name) + binutils | phase 0 |
| `picolibc-riscv64-unknown-elf` | libc for bare-metal target programs | phase 3 |
| `verilator` | SystemVerilog → C++ simulation | phase 1 |
| `qemu-system-misc` | `qemu-system-riscv32` — the third platform of the §3.1 three-platform rule | phase 3 |
| `python3` | test tooling (`words2bin.py`, runners) — preinstalled on Ubuntu | phase 0 |

Later phases add (not needed yet):

- **Phase 2:** SymbiYosys + solvers for riscv-formal — plan: the
  [OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build) bundle,
  which also brings a current Yosys.
- **Phase 7:** `yosys`, `nextpnr-ecp5`, `ecppack` (Trellis) for the FPGA flow
  (also in OSS CAD Suite).

## Known quirks (learned the hard way)

- **`-march` spelling:** distro GCC 10.2 predates the ISA split that moved CSR
  instructions into `Zicsr`. Use `-march=rv32i -mabi=ilp32` — CSR instructions
  are accepted implicitly. Newer toolchains (12+) want `rv32i_zicsr`.
  Makefiles should probe rather than hardcode.
- **Failed installs with 404s:** means stale package lists — run
  `sudo apt update` first (with sudo; without it apt can't take its locks).
  A handful of QEMU audio/GUI dependencies (alsa, gstreamer) may still 404
  harmlessly: we run QEMU headless and never need them.
- **Verilator 4.038** (jammy) is old but sufficient for now; if we hit
  missing-feature walls in phase 1, OSS CAD Suite carries a current build.

## Verify the setup

```bash
# clone with submodules (riscv-tests)
git clone --recurse-submodules <repo-url> && cd atomiX

# build + directed tests
make -C sim/axsim test

# build the official ISA tests, then run them against aXsim
make -C tests/riscv-tests/isa XLEN=32 RISCV_PREFIX=riscv64-unknown-elf- -j"$(nproc)"
tests/run-riscv-tests.sh          # expect: 41 passed, 0 failed
```
