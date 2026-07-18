# sw/user/ — userland

Programs running in U-mode under aXos, against its syscall ABI (static ELF,
no dynamic linking). Phase 5.

Initial set: `init`, `sh`, `ls`, `cat`, `echo` — the minimum for an
interactive system that can inspect itself. Plus a tiny user-side libc
(syscall stubs + string/format helpers), shared with nothing: userland links
only what lives here.

Later: role demo clients (e.g. matmul driving the TPU role from *inside* the
box, complementing host-driven offload).
