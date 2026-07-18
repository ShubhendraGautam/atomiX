# sw/baremetal/ — bare-metal runtime and bring-up programs

The no-OS layer used before (and alongside) the kernel. It uses no libc or
firmware: the image enters at RAM `0x8000_0000`, and talks directly to the
QEMU-virt-aligned UART, CLINT, and test-finisher addresses.

- `crt0.S` — reset entry: set `sp`, zero `.bss`, call `main`
- `link.ld` — linker script for the DESIGN.md §3.2 map (RAM at `0x8000_0000`)
- `include/platform.h` — volatile MMIO helpers, a polling 16550 TX console,
  and the `sifive_test` exit protocol
- `examples/hello.c` — first platform customer; prints then passes
- `traps.S`, `include/csr.h`, `include/clint.h`, and `examples/timer.c` — a
  full-register M-mode trap entry plus a three-tick machine-timer demo
- `examples/preempt.c` — two task contexts on distinct stacks, switched by
  timer interrupts. Its expected UART transcript is `preempt: ABABAB`.

Build and run the current bring-up program:

```bash
make -C sw/baremetal images      # ELF, flat binary, and RTL RAM .hex image
make -C sw/baremetal run-iss
make -C sw/baremetal run-qemu
make -C sw/baremetal run-rtl
make -C sw/baremetal check-hello # asserts identical UART output on all three
make -C sw/baremetal check-timer # CLINT timer interrupts on all three
make -C sw/baremetal check-preempt # timer-preempted task switching on all three
```

`RISCV_PREFIX` defaults to `riscv64-unknown-elf-`. GCC 10 accepts
`RISCV_ARCH=rv32im` (its Zicsr support is included in that spelling); newer
toolchains may be invoked with `RISCV_ARCH=rv32im_zicsr`.

The Phase 3 bare-metal exit demo is now covered by `check-preempt`. It saves
all integer registers into the interrupted task's frame, selects another
frame/`mepc`, restores it, and executes `mret`; this is intentionally a small
and inspectable scheduler substrate rather than a kernel API.
