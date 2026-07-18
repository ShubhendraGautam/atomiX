# sw/kernel/ — aXos

`aXos` is the small monolithic kernel developed in Phase 5. It runs unchanged
on aXsim, QEMU `virt`, and the RTL SoC.

Phase 5 provides Sv32 paging, M/S/U trap transitions, a physical-page
allocator, CLINT-driven preemptive scheduling, and a minimal U-mode process
model. `SYS_FORK` clones the Sv32 root, page table, user stack, and trap
context; `SYS_WAIT` blocks and later reaps the child; `SYS_EXIT` releases every
process page; `SYS_CONSOLE_PUTC` is the first user-visible write syscall.

## Shell and RAM disk

The resident shell runs in S-mode and uses the platform 16550 RX/TX console.
It supports `help`, `ls`, `cat motd`, `cat readme`, `echo`, `fork`, and `exit`.
The initial immutable RAM disk is a named-file table; this keeps the shell
contract independent of its future inode/SD-backed implementation.

`fork` launches the U-mode parent/child demonstration. The child gets return
value zero; the parent receives a child PID, blocks in `wait`, wakes when the
child exits, and reaps it. The shell test accepts either valid first scheduling
order, `PCW` or `CPW`.

## Run and verify

Run the complete shell and fork/wait regression on the ISS, QEMU, and RTL:

```bash
make -C sw/kernel check-boot QEMU="$HOME/.local/bin/qemu-system-riscv32"
```

To run the RTL console with a reproducible command script:

```bash
make -C sw/kernel run-rtl \
  UART_INPUT_FILE="$PWD/sw/kernel/shell_input.txt"
```

The file supplies newline-terminated bytes to the synthesizable UART RX
holding register. Replace it with any command script; `exit` sends the normal
test-finisher success value. QEMU 7 or newer is required with
`-cpu rv32,pmp=false` because aXcore does not implement PMP; setup is in
[docs/toolchain.md](../../docs/toolchain.md).
