# sw/baremetal/ — bare-metal runtime and bring-up programs

The no-OS layer used before (and alongside) the kernel:

- `crt0.S` — reset entry: set `sp`, zero `.bss`, call `main`
- `link.ld` — linker script for the DESIGN.md §3.2 map (RAM at `0x8000_0000`)
- A tiny libc subset: `putchar`/`puts`/`printf` over the 16550 UART,
  `memcpy`/`memset` and friends — only what bring-up needs
- CSR/trap helpers (`csr.h`), CLINT timer helpers
- Bring-up programs: hello-UART, timer-interrupt blinky, the phase 3
  timer-preempted multitasking demo

These programs are the SoC's first customers and double as directed tests in
`tests/`. Phase 0 (built against aXsim) onward.
