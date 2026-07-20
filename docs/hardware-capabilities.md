# Hardware capability matrix

What each supported FPGA target can actually run, per configuration, backed by
real synthesis and simulation runs on this repository — not projections.  Every
row here was produced by an `make`/`yosys` invocation listed at the bottom.

## Evidence levels

Each capability is marked with the strongest evidence that currently backs it:

- **SIM** — the configuration runs correctly in the Verilator RTL simulation
  (functional/architectural correctness).  This is board-independent: it proves
  the RTL, so any board the design *fits* inherits it.
- **SYNTH** — the configuration synthesises and **fits** the specific device
  (Yosys maps it and the resource totals are within the part's budget).
- **BOARD** — proven on physical silicon (place-and-route + bitstream + serial
  transcript).

> **No row is BOARD yet.**  Place-and-route tools are not installed on the build
> host (`nextpnr-ecp5` for ECP5, `nextpnr-himbaechel`/`gowin_pack` for Gowin), so
> the physical-board gate is untaken for both targets.  Everything below is
> SIM + SYNTH: real, but pre-silicon.  SYNTH fit is a synthesis-level resource
> check; final place-and-route may differ, especially near 90%+ utilisation.

## The targets

| Board | FPGA | LUT4 | Flip-flops | Block RAM | DSP (18×18) | Clock |
|---|---|---|---|---|---|---|
| **Tang Nano 20K** | Gowin GW2AR-18C | 20,736 | 15,552 | ~46 × 18 Kb (828 Kb) | ~48 | 27 MHz |
| **ULX3S-85F** | Lattice ECP5 LFE5U-85F | ~83,640 | ~83,640 | 208 × 18 Kb (3.7 Mb) | 156 | 25 MHz |

The two boards differ in main-memory strategy, which the board component fixes:
the Tang Nano is **BRAM-only** (program and data in on-chip block RAM, so RAM
size competes with the accelerator for BSRAM), while the ULX3S uses **external
SDRAM** with caches (fabric holds only a small ROM, leaving block RAM free).

## Tang Nano 20K (Gowin GW2AR-18C) — the small part

Three profiles, each peaked for the part — the CPU, the biggest GPU that fits,
and the TPU:

| Capability | Profile | Config | LUT4 | Block RAM | DSP | Verdict |
|---|---|---|---|---|---|---|
| **CPU** | `tangnano20k` | 5-stage RV32IM/Sv32 | 11.3k | 32 DPB | 0 | ✅ **SYNTH** + **SIM** (hello) |
| **GPU** | `tangnano20k-gpu` | minimal host + 6-lane SIMT | 20.2k (97%) | 32 DPB | 24 | ⚠️ **SYNTH** (fits, tight) + **SIM** (gpu, perf) |
| **TPU** | `tangnano20k-tpu` | 8×8 int8 GEMM | — | — | 64 | ❌ overflow — ~69.5k FF (`cbuf` in flip-flops) |

**Possible on the Tang Nano 20K:** a CPU plus **one** modest accelerator.  The
GPU is peaked at 6 lanes by pairing the wide engine with the minimal host core
(the 5-stage core + 8-lane GPU overflows at ~29k LUT4; the minimal core frees
enough to reach 6 lanes, at 97% — tight).  The TPU does not fit at all, and
all-three is out (the shell also has only one role window).  Other lane counts
are a config away — the catalog ships `role.gpu-compute` (8-lane, overflows
this part), `role.gpu-compute-lite` (4-lane, fits comfortably with the 5-stage
core), and `role.gpu-compute-6` (used here).  Deep analysis:
[tangnano-capacity.md](tangnano-capacity.md).

## ULX3S-85F (Lattice ECP5) — the large part

| Configuration | Profile | LUT4 | Flip-flops | Block RAM | DSP | Verdict |
|---|---|---|---|---|---|---|
| CPU (5-stage RV32IM/Sv32) | `ulx3s-85f` | 10.6k (13%) | 3.1k | — | 0 | ✅ **SYNTH** + **SIM** |
| CPU + GPU (8-lane SIMT) | `ulx3s-85f-gpu` | 23.8k (28%) | 5.5k | 18 EBR | 24 | ✅ **SYNTH** + **SIM** (gpu, perf) |
| CPU + TPU (8×8 int8 GEMM) | `ulx3s-85f-tpu` | (large) | 71.5k (85%) | 2 EBR | 64 | ✅ **SYNTH** (fits; FF-bound) + **SIM** |

**Possible on the ULX3S-85F:** the CPU plus the **full 8-lane GPU** with room to
spare (28% LUT4), and the **TPU** as well (FF-bound at ~85% because its `cbuf`
accumulator still maps to flip-flops rather than block RAM — a role bug, not a
board limit; fixing it would free most of that).  The part has headroom that the
single-role shell does not yet exploit: hosting GPU **and** TPU together needs a
composite role, which does not exist yet.

## Functional coverage (board-independent, SIM)

These prove the RTL that every fitting configuration above inherits:

- Bare-metal: `make -C sw/baremetal check-hello check-timer check-preempt
  check-fencei check-spi check-sd`
- Accelerator roles: `check-role` (loopback), `check-tpu`, `check-gpu`,
  `check-gpu-perf`
- Lock-step cosimulation vs the golden ISS: `make -C sim/cosim test`
- aXos kernel: `make -C sw/kernel check-role-driver check-hostlink`

## Reproducing the fit numbers

```bash
# Tang Nano (Gowin flow); BUILD=<dir> keeps trees separate.
make -C rtl/fpga synth COMPONENT_CONFIG=$PWD/configs/tangnano20k-gpu.json BUILD=build-tn
yosys -p "read_json build-tn/tangnano20k_top.json; stat -top tangnano20k_top"

# ULX3S (ECP5 flow).
make -C rtl/fpga synth COMPONENT_CONFIG=$PWD/configs/ulx3s-85f-gpu.json BUILD=build-u
yosys -p "read_json build-u/ulx3s_85f_top.json; stat"
```
On a memory-constrained host the final mapping passes can thrash; adding
`synth_gowin -run :map_luts; stat` (or `synth_ecp5 -run :map_luts`) reports the
block-RAM / DSP / flip-flop picture without the slowest pass.

_Maintained after each milestone that adds a board target, a role variant, or a
core that changes what fits._
