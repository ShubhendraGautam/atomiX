# rtl/soc/ — the SoC shell

Everything around the core that is **fixed in every bitstream** (the "shell"
of the shell + role platform, DESIGN.md §3.3):

- `axbus_*` — the interconnect: minimal synchronous valid/ready bus, address
  decode, arbiter slot for future DMA/debug masters. Deliberately a
  near-subset of Wishbone classic so a bridge to third-party cores is thin.
- Boot ROM + RAM (BRAM, dual-port to serve ibus and dbus; SDRAM controller
  in phase 6).
- `uart.sv` — 16550-compatible subset (industry standard; matches QEMU-`virt`
  so software runs unchanged on ISS/QEMU/RTL).
- `clint.sv` — timer + software interrupts (`mtime`, `mtimecmp`, `msip`),
  RISC-V-standard programming model. PLIC joins when there is more than one
  external interrupt source.
- Host-link endpoint (USB-serial framing, phase 8) and the role slot that
  `rtl/roles/` designs plug into.
- `axbus_mux.sv` — one fixed-map aXbus decode fabric per aXcore master;
  unmapped accesses complete with an error rather than hanging.
- `axrom.sv`, `axram.sv` — dual-port BRAM-shaped memory blocks. ROM is
  `$readmemh` initialized through `soc_top`'s `ROM_INIT_FILE` parameter.
- `clint.sv` — hart 0 `msip`, `mtimecmp`, and `mtime`, using the QEMU-virt
  offsets and raising core software/timer interrupt lines.
- `uart.sv` — 16550-style THR/RBR plus LSR subset. A one-byte RX holding
  register reports LSR.DR and is driven by the simulation console sideband;
  byte registers are packed correctly into aXbus's word-aligned read lanes.
- `test_finisher.sv` — synthesizable simulation endpoint for QEMU's
  `sifive_test` pass/fail convention.
- `soc_top.sv` — ties the shell together; reset defaults to boot ROM
  (`0x0000_1000`).

The memory map is QEMU-`virt`-aligned — see DESIGN.md §3.1/§3.2. Built in
phase 3. The currently implemented shell is covered by:

```bash
make -C sim/unit run-soc        # ROM, RAM, UART, and finisher
make -C sim/unit run-soc-timer  # CLINT -> precise timer interrupt -> handler
```
