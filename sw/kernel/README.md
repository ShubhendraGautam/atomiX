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
