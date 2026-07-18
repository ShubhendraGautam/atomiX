# rtl/fpga/ — board targets

Board-specific top-levels, pin constraints (LPF), and clocking (PLL setup).
This is the **only** place vendor/board-specific code is allowed; everything
under `core/`, `soc/`, `roles/` stays board-agnostic.

Flow: **Yosys → nextpnr-ecp5 → ecppack** (fully open toolchain), targeting
Lattice ECP5. Standing board favorite: ULX3S (85F); purchase deferred to
phase 6/7 — until then this directory holds a *virtual* target's constraints
used to keep synthesis honest (utilization and timing reports in CI).

Contents per board: `<board>_top.sv`, `<board>.lpf`, build Makefile,
bitstream-flash notes.
