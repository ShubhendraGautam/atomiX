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
It supports `help`, `ls`, `cat NAME`, `write NAME TEXT`, `echo`, `fork`, and
`exit`. The initial immutable RAM disk is a named-file table. Phase 6 adds an
optional AXFS v1 SD image path: on cached external-memory RTL, `check-storage`
mounts `motd` and `readme` through the kernel SPI block driver. `write` creates
or replaces one sector-sized AXFS file through SD CMD24; it is deliberately not
a crash-safe general filesystem. The RAM disk remains the ISS/QEMU fallback.

`fork` launches the U-mode parent/child demonstration. The child gets return
value zero; the parent receives a child PID, blocks in `wait`, wakes when the
child exits, and reaps it. The shell test accepts either valid first scheduling
order, `PCW` or `CPW`.

## Run and verify

Run the complete shell and fork/wait regression on the ISS, QEMU, and RTL:

```bash
make -C sw/kernel check-boot QEMU="$HOME/.local/bin/qemu-system-riscv32"
```

Phase 6's board-independent memory regression retains the same kernel image,
but runs it on 32 MiB of delayed RAM through optional I/D caches:

```bash
make -C sw/kernel check-memory
make -C sw/kernel check-storage
make -C sw/kernel check-storage-write
make -C sw/kernel check-sdboot
```

`check-sdboot` builds `build/axos_boot.img`, a bootable SD-card image with the
kernel at its ROM-loader location and AXFS at sector 64. It then proves the
ROM loader, the real `axsdram` pin-level controller model, and the mounted
shell in one RTL run. See [docs/ulx3s-bringup.md](../../docs/ulx3s-bringup.md)
to use the same image on an ULX3S.

To run the RTL console with a reproducible command script:

```bash
make -C sw/kernel run-rtl \
  UART_INPUT_FILE="$PWD/sw/kernel/shell_input.txt"
```

For the delayed external-memory model and caches, add
`RAM_BYTES=33554432 EXTERNAL_MEMORY=1 CACHES=1`. The fork/wait script needs a
larger runner budget: add `MAX_CYCLES=500000`.

The file supplies newline-terminated bytes to the synthesizable UART RX
holding register. Replace it with any command script; `exit` sends the normal
test-finisher success value. QEMU 7 or newer is required with
`-cpu rv32,pmp=false` because aXcore does not implement PMP; setup is in
[docs/toolchain.md](../../docs/toolchain.md).
Memory-model and cache design details are in [docs/memory.md](../../docs/memory.md).
