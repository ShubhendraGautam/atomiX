# sw/host/ — axhost, the host-side driver

The software that runs on your PC (Linux) and makes the FPGA a managed
accelerator card (phase 8+):

- `axhost` daemon/CLI: opens the USB link, speaks the framed
  `docs/host-protocol.md` shell protocol — bitstream upload (mode switch),
  buffer read/write, work submission, completion events
- Per-role client libraries layered on top (e.g. `libaxtpu`: a matmul API
  that marshals tensors into role descriptors)
- The virtual-pipe backend: the same code talks to a Verilator simulation
  through a pty/socket instead of USB, so the entire host→shell→role path
  runs before hardware exists

Design rule: `axhost` knows the **shell protocol only** — never role
internals. Role knowledge lives in aXos and in the per-role libraries.
Userspace only (plain USB-serial); no kernel module unless PCIe ever happens.
