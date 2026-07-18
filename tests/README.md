# tests/ — test suites

- `riscv-tests/` — the official RISC-V ISA test suite (git submodule). The
  industry-standard correctness baseline: rv32ui (base ISA), rv32mi (M-mode
  traps), later rv32um (M ext) and rv32si (S-mode). Run against **both**
  aXsim and the RTL.
  - Build: `make -C riscv-tests/isa XLEN=32 RISCV_PREFIX=riscv64-unknown-elf-`
    (the `-p` variants are what we run; `-v` needs the phase-4 VM environment).
  - Run: `./run-riscv-tests.sh` (env `SIM=` selects the simulator).
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
