# rtl/ — synthesizable hardware

All hardware description lives here, in **SystemVerilog restricted to the
Yosys-synthesizable subset**: synchronous single-clock design, BRAM-shaped
memories, no latches. Everything must pass both Verilator (simulation) and
Yosys (synthesis) — that dual gate is what keeps us FPGA-portable.

| Subdirectory | Role |
|---|---|
| [core/](core/) | `aXcore` — the RV32 CPU |
| [soc/](soc/) | The SoC **shell**: aXbus, memories, peripherals, top-level |
| [roles/](roles/) | Swappable accelerator roles (loopback, TPU-lite, …) |
| [fpga/](fpga/) | Board-specific top-levels and constraints |

Conventions:

- One module per file, filename = module name.
- No vendor primitives outside `fpga/`; inference (BRAM, DSP) over instantiation.
- Every module gets at least a smoke testbench under `sim/` before integration.
