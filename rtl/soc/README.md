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
- `soc_top.sv` — ties it together; the simulation top-level.

The memory map is QEMU-`virt`-aligned — see DESIGN.md §3.1/§3.2. Built in
phase 3.
