# Toolchain and host setup

Everything needed to build, simulate, and formally verify atomiX, captured
from the actual bring-up on Ubuntu 22.04 (jammy) under WSL2. The compiler,
simulator, and SMT solvers are distro packages. SymbiYosys (SBY),
riscv-formal, and a current Yosys are installed from upstream because Ubuntu
22.04's Yosys 0.9 cannot parse the SystemVerilog packages used by aXcore.

## Install (Ubuntu/Debian)

```bash
sudo apt update          # do this first: stale lists cause 404s mid-install
sudo apt install \
  build-essential \
  gcc-riscv64-unknown-elf \
  picolibc-riscv64-unknown-elf \
  verilator \
  qemu-system-misc \
  boolector \
  z3 \
  git \
  make \
  python3
```

| Package | Provides | Needed from |
|---|---|---|
| `build-essential` | host `g++`, `make` — builds aXsim and the cosim harness (`clang++` works too; the Makefile auto-detects) | phase 0 |
| `gcc-riscv64-unknown-elf` | the RISC-V cross-compiler (multilib: targets rv32 despite the name) + binutils | phase 0 |
| `picolibc-riscv64-unknown-elf` | optional libc for later, richer bare-metal programs; the current runtime is freestanding and does not need it | later phase 3 |
| `verilator` | SystemVerilog → C++ simulation | phase 1 |
| `qemu-system-misc` | `qemu-system-riscv32` — the third platform of the §3.1 three-platform rule | phase 3 |
| `boolector`, `z3` | optional alternate SMT solvers for exploratory formal work | phase 2 |
| `git`, `make`, `python3` | test runners and upstream formal-tool installation | phases 0–2 |

## Formal tools (phase 2)

The Ubuntu 22.04 `yosys` package is useful for simple Verilog, but its 0.9
parser fails on `package`/`import` in this RTL. Install current Yosys before
running `make -C formal check`. This is the one case where cloning Yosys is
required even if `apt install yosys` already succeeded. The guards refuse to
overwrite existing `/opt` installations.

```bash
# Prerequisites for a current upstream Yosys.
sudo apt install --yes --no-install-recommends \
  gawk git make python3 lld bison clang flex libffi-dev libfl-dev \
  libreadline-dev pkg-config tcl-dev zlib1g-dev graphviz xdot

# Current Yosys uses CMake >= 3.28. Ubuntu 22.04 packages only 3.22.
sudo snap install cmake --classic

test ! -e /opt/yosys
sudo git clone --depth 1 --recurse-submodules \
  https://github.com/YosysHQ/yosys.git /opt/yosys

# Allow the cloning user to configure and build; installation alone uses sudo.
sudo chown -R "$USER":"$USER" /opt/yosys
/snap/bin/cmake -S /opt/yosys -B /opt/yosys/build -DCMAKE_BUILD_TYPE=Release
/snap/bin/cmake --build /opt/yosys/build --parallel "$(nproc)"
sudo /snap/bin/cmake --install /opt/yosys/build --strip
hash -r

# SymbiYosys and the reference checker framework.
test ! -e /opt/sby
test ! -e /opt/riscv-formal

sudo git clone --depth 1 https://github.com/YosysHQ/sby.git /opt/sby
sudo make -C /opt/sby PREFIX=/usr/local install
sudo git clone --depth 1 --recurse-submodules \
  https://github.com/YosysHQ/riscv-formal.git /opt/riscv-formal
```

`/opt/yosys` installs the current `yosys` into `/usr/local/bin`, ahead of the
distro version on a normal Ubuntu `PATH`. `/opt/sby` similarly places `sby` in
`/usr/local/bin`. `/opt/riscv-formal` remains an external reference checkout;
the atomiX repository never modifies it.

## ECP5 FPGA tools (Phase 7)

The ULX3S target needs the ECP5-specific tools `nextpnr-ecp5` and `ecppack`
from Project Trellis in addition to Yosys. Ubuntu 22.04 packages generic and
iCE40 nextpnr variants, but not `nextpnr-ecp5`; installing the `yosys` package
alone cannot build this board. The maintained and fast route is the prebuilt
[OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build/releases), not
an hour-long local build of the whole FPGA stack.

1. Download the current Linux x86_64 OSS CAD Suite archive from that official
   release page and verify its published checksum.
2. Extract it locally and enable it for the current shell (substitute the
   archive's actual release name):

```bash
mkdir -p "$HOME/opt"
tar -C "$HOME/opt" -xf "$HOME/Downloads/oss-cad-suite-linux-x64-<release>.tgz"
source "$HOME/opt/oss-cad-suite/environment"
```

The environment script supplies a matched `yosys`, `nextpnr-ecp5`, and
`ecppack`; do not mix arbitrary distro and suite binaries inside one FPGA
build. Add the `source` line to your shell profile only after confirming the
commands below resolve to the extracted suite:

```bash
command -v yosys nextpnr-ecp5 ecppack
yosys -V
nextpnr-ecp5 --version
ecppack --version
make -C rtl/fpga
```

`openFPGALoader` is separate from synthesis/P&R and is needed only to send the
finished bitstream to a board. Its Linux installation choices are maintained
on the [official release page](https://github.com/trabucayre/openFPGALoader/releases).
Ubuntu 22.04's configured package repositories do not provide it, so prefer a
verified upstream release artifact or the project's documented source build.
Install the serial-console helper with the regular package manager:

```bash
sudo apt install picocom
```

Keep the programmer executable on `PATH`, then verify it without programming
anything:

```bash
openFPGALoader --version
```

The reproducible target is `make -C rtl/fpga`; `program` is a reversible SRAM
configuration and `flash` is explicitly persistent. The complete hardware
sequence and SD-card safety check are in [ulx3s-bringup.md](ulx3s-bringup.md).

## Known quirks (learned the hard way)

- **`-march` spelling:** distro GCC 10.2 predates the ISA split that moved CSR
  instructions into `Zicsr`. Use `-march=rv32im -mabi=ilp32` — CSR instructions
  are accepted implicitly. Newer toolchains can use `rv32im_zicsr`. The
  bare-metal Makefile defaults to the GCC-10-compatible spelling and exposes
  `RISCV_ARCH` for an override.
- **Failed installs with 404s:** means stale package lists — run
  `sudo apt update` first (with sudo; without it apt can't take its locks).
  A handful of QEMU audio/GUI dependencies (alsa, gstreamer) may still 404
  harmlessly: we run QEMU headless and never need them.
- **riscv-tests `-v` (virtual-memory) builds:** the env/v harness includes
  `string.h`/`stdint.h`, which the Ubuntu `gcc-riscv64-unknown-elf` package
  doesn't put on the default include path — they live in the companion
  picolibc package. Build the `-v` targets with the extra flag
  `-isystem /usr/lib/picolibc/riscv64-unknown-elf/include` appended to
  `RISCV_GCC_OPTS` (tests/README.md shows the full invocation).
- **Verilator 4.038** (jammy) is old but sufficient for now.
- **Yosys 0.9** (the Ubuntu 22.04 package) is *not* sufficient for this
  project's formal flow: it rejects `axcore_pkg.sv` with a `TOK_TYPEDEF`
  parser error. Confirm `command -v yosys` resolves to `/usr/local/bin/yosys`
  after the upstream installation.
- **FPGA SystemVerilog frontend:** the checked-in FPGA Makefile invokes
  `prepare_synth.py` before Yosys because the frontend still rejects the
  project's `import axcore_pkg::*` form. The script generates a disposable
  width-equivalent file under `rtl/fpga/build/`; it never modifies authored
  RTL. This is expected, not a reason to edit the source package away.

## Verify the setup

```bash
# clone with submodules (riscv-tests)
git clone --recurse-submodules <repo-url> && cd atomiX

# build + directed tests
make -C sim/axsim test

# build the first freestanding C image and compare its UART output on ISS,
# QEMU virt, and the complete Verilated SoC
make -C sw/baremetal check-hello
make -C sw/baremetal check-timer
make -C sw/baremetal check-preempt

# confirm the formal toolchain and reference checkout
sby --version
command -v yosys                 # expect: /usr/local/bin/yosys
yosys -V
boolector --version
z3 --version
test -d /opt/riscv-formal && echo "riscv-formal: OK"
make -C formal check            # bounded RVFI/riscv-formal suite

# build the official ISA tests, then run them against aXsim
make -C tests/riscv-tests/isa XLEN=32 RISCV_PREFIX=riscv64-unknown-elf- -j"$(nproc)"
tests/run-riscv-tests.sh          # expect: 41 passed, 0 failed

# Phase-4 supervisor and virtual-memory regressions
tests/run-riscv-tests.sh rv32si   # expect: 6 passed, 0 failed
make -C sim/unit run-privilege    # M/S/U transitions, delegation, sret
make -C sim/unit run-sv32         # Sv32 walk, A/D update, page fault
make -C sim/testgen paging        # 10 seeds × 10k user-mode Sv32 instructions
SIM=../sim/cosim/obj_dir/axcosim tests/run-riscv-tests.sh rv32si

# Phase-5 aXos shell plus U-mode fork/wait on ISS, QEMU, and RTL
make -C sw/kernel check-boot QEMU="$HOME/.local/bin/qemu-system-riscv32"

# Phase-6 storage/SDRAM regressions and the Phase-7 synthesis preflight
make -C sim/unit run-axsdram run-axuart-phy
make -C sw/kernel check-storage-write check-sdboot
source "$HOME/opt/oss-cad-suite/environment"  # if using the suite above
make -C rtl/fpga
```

## QEMU required for Phase 5

The Ubuntu 22.04 `qemu-system-riscv32` package is 6.2. Its RISC-V model has a
known upstream PMP bug: `mret` into S/U mode traps when no PMP entries are
configured, even when the CPU is requested with `pmp=false`. aXcore does not
implement optional PMP, so the first S-mode kernel needs **QEMU 7 or newer**.

Build a small, RISC-V-only current QEMU locally; this does not replace the
distro package and needs no `sudo`:

```bash
sudo apt update
sudo apt install build-essential git meson ninja-build pkg-config python3 python3-venv \
  libglib2.0-dev libpixman-1-dev zlib1g-dev libfdt-dev
git clone --depth 1 --branch v8.2.10 https://github.com/qemu/qemu.git /tmp/qemu-8.2.10
cd /tmp/qemu-8.2.10
mkdir build
./configure --target-list=riscv32-softmmu --prefix="$HOME/.local"
ninja -C build
ninja -C build install
export PATH="$HOME/.local/bin:$PATH"
qemu-system-riscv32 --version       # expect 8.2.x (or any version >= 7)
```

The source tree under `/tmp` may be removed after installation. Keep the
`PATH` export in your shell profile if you want this QEMU to be the default.
Then verify the platform-neutral aXos shell and fork/wait sessions:

```bash
make -C sw/kernel check-boot QEMU="$HOME/.local/bin/qemu-system-riscv32"
```
