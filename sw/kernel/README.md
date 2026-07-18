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
`aXos: S-mode Sv32 preemptive scheduler online` and exits through the usual
finisher. The
machine timer is acknowledged in a tiny M-mode shim and turned into a
delegated supervisor software interrupt; the S-mode trap entry saves every
GPR, acknowledges it, and resumes. The kernel then exercises every available
4 KiB physical RAM page through its free-list allocator, while reserving one
page for the live bootstrap/trap stack. Finally, two S-mode tasks run on their
own allocated stacks; every CLINT tick saves the interrupted trap frame and
round-robins to the other task. This is the foundation for user processes and
syscalls.

Run it with `make check-boot`. If the local QEMU install is not on `PATH`,
pass it explicitly: `make check-boot QEMU="$HOME/.local/bin/qemu-system-riscv32"`.

The Ubuntu 22.04 QEMU 6.2 package has an upstream RISC-V PMP bug that rejects
`mret` to S/U when no PMP region exists. This project does not implement PMP,
so the cross-platform kernel check requires QEMU 7 or newer with
`-cpu rv32,pmp=false`; setup is in [docs/toolchain.md](../../docs/toolchain.md).
