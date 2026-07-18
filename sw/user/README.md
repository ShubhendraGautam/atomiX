# sw/user/ — userland

Programs running in U-mode under aXos, against its syscall ABI (static ELF,
no dynamic linking).

The resident aXos shell and U-mode fork/wait process demo currently live in
`sw/kernel/`; the shell supplies `ls`, `cat`, and `echo` over its RAM disk.
This directory becomes the home for separately linked `init`, `sh`, and
coreutils once the executable loader and persistent filesystem arrive with
real memory/SD work.

Later: role demo clients (e.g. matmul driving the TPU role from *inside* the
box, complementing host-driven offload).
