# sw/kernel/ — aXos

Our monolithic Unix-like kernel: **xv6-inspired in scope** (the known-good
reference when stuck), our own code. Roadmap phase 5, developed on aXsim/QEMU
in parallel with earlier RTL phases.

Core scope (DESIGN.md §7): boot → Sv32 paging on → trap handling → processes
with round-robin preemptive scheduling (CLINT timer) → syscalls
(fork/exec/exit/wait/read/write/pipe) → inode filesystem (RAM-disk first, SD
later) → serial console shell.

**Platform duties** (the pivot, DESIGN.md §3.3) — aXos is also the resident
OS of the accelerator card in every mode:

- Role service: discover the attached role by ID register, manage its
  descriptor ring, feed it work, handle its completions
- Host-link service: speak the `docs/host-protocol.md` framed protocol with
  `axhost` over USB — buffer transfer, work submission, events

Exit criterion (phase 5): interactive shell on the RTL simulation console.

## Bootstrap milestone

`make check-boot` builds the first aXos image. M-mode constructs an Sv32
identity map for kernel RAM plus UART, CLINT, and the test finisher; it then
enters S-mode with `mret`. The S-mode kernel prints
`aXos: S-mode Sv32 online` and exits through the usual finisher. It is the
foundation for the S-mode trap path, timer shim, allocator, and process work.

Run it with `make check-boot`. If the local QEMU install is not on `PATH`,
pass it explicitly: `make check-boot QEMU="$HOME/.local/bin/qemu-system-riscv32"`.

The Ubuntu 22.04 QEMU 6.2 package has an upstream RISC-V PMP bug that rejects
`mret` to S/U when no PMP region exists. This project does not implement PMP,
so the cross-platform kernel check requires QEMU 7 or newer with
`-cpu rv32,pmp=false`; setup is in [docs/toolchain.md](../../docs/toolchain.md).
