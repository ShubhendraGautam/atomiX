# tests/ — test suites

- `riscv-tests/` — the official RISC-V ISA test suite (git submodule). The
  industry-standard correctness baseline: rv32ui (base ISA), rv32mi (M-mode
  traps), later rv32um (M ext) and rv32si (S-mode). Run against **both**
  aXsim and the RTL.
  - Build: `make -C riscv-tests/isa XLEN=32 RISCV_PREFIX=riscv64-unknown-elf-`
    builds the `-p` (physical) variants. The `-v` (Sv32 demand-paging)
    variants additionally need libc headers the Ubuntu cross package hides in
    picolibc's directory — build them per-target with
    `RISCV_GCC_OPTS="-static -mcmodel=medany -fvisibility=hidden -nostdlib
    -nostartfiles -isystem /usr/lib/picolibc/riscv64-unknown-elf/include"`
    (docs/toolchain.md has the full story).
  - Run: `./run-riscv-tests.sh` (env `SIM=` selects the simulator). Suite
    args select more: `rv32si` (S-mode, ISS-only until the phase-4 RTL),
    `rv32um`, and `rv32ui-v`/`rv32um-v` for the virtual-memory environment.
  - Policy exclusion: `rv32ui-p-ma_data` expects hardware misaligned-access
    support; atomiX traps on misaligned (like Spike without `--misaligned`),
    which `rv32mi-p-ma_addr` verifies instead.
- `directed/` — our own regression tests: hazard corner cases, trap-on-branch
  shadows, bus wait-state behavior, CSR serialization — plus every failing
  seed testgen ever finds, pinned forever.
- Runner scripts/Makefiles that execute a suite on a chosen platform
  (aXsim | QEMU | Verilated RTL) and diff results across platforms.

Convention: a bug isn't fixed until a test here fails without the fix and
passes with it.
