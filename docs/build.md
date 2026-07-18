# Build and verification guide

This guide is the short path from a fresh checkout to useful evidence.  It
does not replace the component contracts: select a profile first, then run the
checks that exercise the part you changed.

Before starting, install the appropriate tools in
[dependencies.md](dependencies.md).  Exact host-setup instructions and known
tool quirks live in [toolchain.md](toolchain.md).

## 1. Choose a system profile

Profiles are JSON manifests that select compatible implementations.  The
stock simulation profiles are the quickest starting point.

```bash
make component-list
make config-check CONFIG=configs/sim-bram.json
make config-check-all
```

Read [components/README.md](../components/README.md) before introducing a new
implementation, and see [configs/README.md](../configs/README.md) for the
available profiles and overrides.

## 2. Fast first build

Build the instruction-set simulator and a small bare-metal image, then run the
same image on the selected RTL SoC.

```bash
make -C sim/axsim test
make -C sw/baremetal images
make sim CONFIG=configs/sim-bram.json \
  RAM_INIT_FILE="$PWD/sw/baremetal/build/hello.hex"
```

For the established three-platform check (ISS, QEMU, and RTL):

```bash
make -C sw/baremetal check-hello
make -C sw/baremetal check-timer
make -C sw/baremetal check-preempt
```

## 3. Simulation and composition checks

Use the narrowest check that covers a change, then run the composition suite
before declaring a component or profile ready.

```bash
make -C sim/unit test
make -C sim/cosim test
make component-test
```

`component-test` exercises every checked-in configuration, including the
reference cache/memory paths and aXos storage composition.  It is intentionally
slower than the directed unit checks.

Long randomized lock-step testing is available when changing core execution or
memory translation:

```bash
make -C sim/testgen fuzz
make -C sim/testgen paging
```

## 4. Kernel and storage checks

aXos S/U-mode checks require `qemu-system-riscv32` version 7 or newer.  Pass
the absolute executable path if a suitable local QEMU is not first on `PATH`.

```bash
make -C sw/kernel check-boot QEMU=/path/to/qemu-system-riscv32
make -C sw/kernel check-storage-write
make -C sw/kernel check-sdboot
make -C sw/kernel kernel-component-test QEMU=/path/to/qemu-system-riscv32
```

The exact images, expected UART transcripts, and selectable aXos services are
documented in [sw/kernel/README.md](../sw/kernel/README.md).

## 5. Formal verification

Formal jobs are an additional gate for RTL changes; they are not a substitute
for the architectural cosimulation suite.

```bash
make -C formal check
```

Install current upstream Yosys, SymbiYosys, and riscv-formal first; Ubuntu
22.04's packaged Yosys is too old for this flow.  See
[toolchain.md](toolchain.md#formal-verification).

## 6. FPGA build and physical hardware

The generic ECP5 flow is separate from selected board implementations.  It
requires the ECP5 tools supplied by OSS CAD Suite.

```bash
source "$HOME/opt/oss-cad-suite/environment"
make fpga CONFIG=configs/ulx3s-85f.json
```

Do not program a board merely because synthesis passes.  Complete the final
hardware gate in [design-checklist.md](design-checklist.md) and follow the
reversible programming procedure in [ulx3s-bringup.md](ulx3s-bringup.md).
