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
this part), `role.gpu-compute` at 4 lanes (fits comfortably with the
5-stage core), and at 6 lanes (used here).  Deep analysis:
[tangnano-capacity.md](tangnano-capacity.md).

## ULX3S-85F (Lattice ECP5) — the large part

| Configuration | Profile | LUT4 | Flip-flops | Block RAM | DSP | Verdict |
|---|---|---|---|---|---|---|
| CPU (5-stage RV32IM/Sv32) | `ulx3s-85f` | 10.6k (13%) | 3.1k | — | 0 | ✅ **SYNTH** + **SIM** |
| minimal host + GPU (**16-lane** SIMT) | `ulx3s-85f-gpu` | 35.4k (42%) | 5.9k | 18 EBR | 48 | ✅ **SYNTH** + **SIM** (suite) |
| CPU + TPU (8×8 int8 GEMM) | `ulx3s-85f-tpu` | (large) | 71.5k (85%) | 2 EBR | 64 | ✅ **SYNTH** (fits; FF-bound) + **SIM** |

**Possible on the ULX3S-85F:** the CPU plus a **wider GPU** — the profile takes
the engine to 16 lanes (42% LUT4, 48 of 156 DSP), using headroom the small Tang
Nano does not have — and the **TPU** as well (FF-bound at ~85% because its `cbuf`
accumulator still maps to flip-flops rather than block RAM — a role bug, not a
board limit; fixing it would free most of that).  The part has headroom that the
single-role shell does not yet exploit: hosting GPU **and** TPU together needs a
composite role, which does not exist yet.

### Lane scaling has diminishing returns (measured)

The engine has **one global-buffer port**, so the memory throughput — not the
lane count — sets the ceiling.  Two things follow.

**Optimisation delivered — pipelined loads.**  The block-RAM read is registered,
and `LDX` used to spend 2 cycles per lane (present address, then capture).  It is
now pipelined: address `p` is presented while lane `p-1`'s data is captured, so
N lane-loads cost N+1 cycles instead of 2N — single-port optimal.  This is a
flat ~1.35× at every lane count (16-lane `poly` 1827 → 1350 GPU cycles, `saxpy`
1683 → 1206 at N=256), verified against the on-core oracle at 6/8/16 lanes.

**Still bottlenecked past ~8 lanes.**  Even with pipelined loads the single port
services one lane per cycle, so doubling lanes doubles the per-wave memory time
over half as many waves — memory time is flat, only the parallel ALU/multiply
portion scales:

| kernel (N=256, GPU cycles, pipelined) | 8-lane | 16-lane | speedup |
|---|---|---|---|
| `poly` (compute-heavy) | 1901 | 1350 | 1.41× |
| `saxpy` (memory-heavy) | 1613 | 1206 | 1.34× |

## Scaling the accelerator: banked memory (measured)

The single-port ceiling above is what `role.gpu1-*` removes.  Its global buffer
is split into NBANKS interleaved block RAMs behind a lane→bank crossbar, so a
coalesced access — lane L touching `base+L`, the common SIMT pattern — hits
NBANKS distinct banks and retires in one round instead of one per lane.  Bank
conflicts serialise, lowest-lane-first, which is what preserves the
ascending-lane store order the oracle depends on.

saxpy, 50 threads, GPU cycles (lower is better).  Both engines are single
tunable components, so these are parameter settings:

| configuration | saxpy cycles | vs 8-lane single-port |
|---|---|---|
| `gpu-compute` 4 lanes, 1 port | 503 | 0.71× |
| `gpu-compute` 8 lanes, 1 port | 359 | 1.00× |
| `gpu-compute` 16 lanes, 1 port | 305 | 1.18× |
| `gpu1` 4 lanes / 4 banks | 347 | 1.03× |
| `gpu1` 8 lanes / 8 banks | 191 | 1.88× |
| `gpu1` 16 lanes / 4 banks | 140 | 2.56× |
| `gpu1` 16 lanes / 16 banks | 113 | 3.18× |
| `gpu1` 32 lanes / 32 banks | 62 | 5.79× |

The 16-lane rows separate the two effects: 16 lanes with only 4 banks reaches
2.56×, and widening the banks to match the lanes takes it to 3.18×.  Lanes and
banks are independent knobs and the memory side is the one that was missing.

Per doubling of lanes, the single-port engine gains 1.18× (8→16); gpu1 gains
1.69–1.82×.  That is the difference between a lane count that is worth raising
and one that is not, and it is why the gpu1 tiers go to 32 lanes while the
gpu-compute family stopped at 16.

`role.gpu1-*` also adds the control ISA the old engine lacked — structured
per-lane divergence (IF/ELSE/ENDIF), uniform and any-lane branches, compare-set,
integer divide, cross-lane shuffle, and displaced addressing — so kernels can
branch and loop instead of being straight-line only.

Reproduce: `python3 tools/bench.py gpu`.

## CPU scaling: dual issue (measured)

`core.ax2-*` fetches a two-instruction bundle through a block-RAM instruction
cache — the aXbus fetch port is 32 bits wide, so no bus-fed core can sustain
more than one instruction per cycle regardless of back-end width — and issues
both when they are independent.

Retired instructions per cycle, `sw/baremetal/examples/cpu_perf.c`.  `core.ax2`
is one component, so these rows are parameter settings, not variants:

| configuration | alu | chain | branch | memcpy | mixed | total cycles | vs `core.minimal` |
|---|---|---|---|---|---|---|---|
| `core.minimal` | 0.50 | 0.50 | 0.50 | 0.43 | 0.45 | 92,348 | 1.00× |
| `core.pipeline5` | 0.77 | 0.83 | 0.66 | 0.77 | 0.84 | 58,314 | 1.58× |
| ax2 1-wide, 1K I$, no BTB | 0.77 | 0.83 | 0.66 | 0.77 | 0.84 | 57,937 | 1.59× |
| ax2 1-wide, 2K I$, BTB 32 | 0.99 | 0.99 | 0.99 | 0.99 | 0.99 | 45,748 | 2.02× |
| ax2 2-wide, 2K I$, no BTB | 1.15 | 0.90 | 0.79 | 0.99 | 0.99 | 48,498 | 1.90× |
| **ax2 defaults** (2-wide, 2K I$, BTB 32) | 1.71 | 1.10 | 1.32 | 1.39 | 1.21 | 36,568 | **2.53×** |
| ax2 2-wide, 8K I$, BTB 128 | 1.71 | 1.10 | 1.32 | 1.39 | 1.21 | 36,540 | 2.53× |

What each knob is actually worth, which a fixed set of tiers could not have
shown:

- **Stripped to 1-wide with a minimal cache and no predictor, ax2 lands exactly
  on `core.pipeline5`** (0.77/0.83, 1.59×).  That is the honest baseline: the
  dual-issue machinery contributes nothing until it is turned on.
- **The instruction cache is the single biggest knob.**  1K→2K plus a predictor
  takes a 1-wide core from 0.77 to 0.99 IPC — pipeline5 loses about a third of
  its cycles to fetch, and caching recovers nearly all of it before any second
  issue slot exists.
- **The predictor outranks the second issue slot on branchy code.**  2-wide
  without a BTB scores 0.79 on `branch`; 1-wide *with* one scores 0.99.  Issue
  width alone can lose to prediction alone.
- **`chain` never exceeds 1.10** — it is a serial dependency chain, so slot 1
  cannot fill.  It is in the suite to bound the claim.
- **8K I$ and a 128-entry BTB measure identically to 2K/32.**  This benchmark's
  working set fits in 2 KiB; the larger settings are for larger programs and
  this measurement is not evidence for them.

Reproduce: `python3 tools/bench.py cpu`.

## Memory system: what a real program actually hits (measured)

The IPC table above is measured on a working set that fits in cache, which
flatters any core with a good front end.  `render_perf.c` exists because that
number does not predict whether a real program runs well.  It is shaped like a
1993 software renderer -- texture-mapped column and span fills over a ~52 KiB
working set, a perspective divide per column, and a sequential framebuffer pass
-- run against delayed external memory.

Cycles per pixel (per divide for `fixdiv`), lower is better:

| configuration | column | span | fixdiv | blit | total | vs baseline |
|---|---|---|---|---|---|---|
| 256 B write-through $, div/32 | 24.93 | 31.03 | 37.01 | 25.00 | 9,323,481 | 1.00× |
| 16 KiB write-through $, div/32 | 16.83 | 21.63 | 37.01 | 25.00 | 7,119,856 | 1.31× |
| 16 KiB write-through $, div/16 | 16.58 | 21.63 | 21.01 | 25.00 | 7,035,384 | 1.33× |
| 16 KiB **write-back** $, div/16 | 13.13 | 18.51 | 21.01 | **3.92** | 4,515,432 | 2.06× |
| 32 KiB write-back $, div/16, 8-word line | 10.03 | 12.42 | 21.01 | **3.24** | 3,208,416 | **2.91×** |
| `core.minimal`, same memory | 26.22 | 33.47 | 30.01 | 12.24 | 8,756,530 | 1.06× |

Three things this measurement established, none of which were visible from the
IPC benchmark:

- **Cache policy dominated cache size.**  Growing the write-through cache 64×
  (256 B → 16 KiB) bought 1.31×.  Changing the policy at the *same* size bought
  another 1.55× on top.  `blit` is the tell: it sat at exactly 25.00 cycles per
  pixel at every write-through size, and got *worse* (41.00) with longer lines.
  The reference cache invalidates a line on write and does not write-allocate,
  so a sequential byte read-modify-write invalidates the line it just filled and
  every byte pays a full miss plus a full memory write.  `cache.writeback` takes
  that to 3.24 — a 7.7× swing on the workload a framebuffer generates.
- **Divider latency is worth about 1.02× overall** but 1.76× on the divide
  itself.  `muldiv.radix4` (16-cycle divide) matters to code that divides in an
  inner loop and is close to free elsewhere; it is not a general-purpose win.
- **A wide core does not rescue a bad memory system.**  `core.minimal` on the
  best memory configuration lands within 6% of the *baseline* — and dual-issue
  ax2 on the baseline memory is barely better than minimal.  The core and the
  memory system have to be sized together.

Reproduce: `python3 tools/bench.py render`.

> `cache.writeback` must not be paired with `core.pipeline5` using Sv32: that
> core's hardware page-table walker writes PTE A/D bits through the *fetch*
> port, and a drain could write a stale line back over such an update.  Cores
> with no fetch-port writes (`core.ax2-*`, `core.minimal`) are safe.  The
> constraint is stated on the component.

## Optimisation roadmap — maximising the large part

Ranked levers to push the ULX3S further, with what each unblocks.  Design-stage
notes (options, correctness constraints, open questions) for the open items are
in [optimization-design.md](optimization-design.md).

1. **Pipelined `LDX`** — ✅ done (~1.35× flat, above).
2. **Multi-bank global memory** — ✅ done, shipped as `role.gpu1-*` (above).
3. **TPU `cbuf` → block RAM** — frees ~65k flip-flops (the accumulator currently
   flattens to FFs; structurally it matches the GPU `gmem`, which *does* infer
   BRAM, so the cause needs a synth-level diagnosis).  Makes the TPU cheap.
4. **Composite GPU + TPU role** — with (3) done the FF budget allows both engines
   behind one role window (needs a composite role; the shell has one window
   today), so the large part runs CPU + GPU + TPU together — impossible on the
   Tang Nano.

Catalog: cores `core.ax2` (tunable, above), `core.pipeline5` (reference — the
only one with Sv32/S-U and cosim/formal evidence), `core.minimal` (smallest).
Roles `role.gpu1` (banked, tunable) and `role.gpu-compute` (single-port,
tunable).  Sizes are parameters on these components, not separate components:
see [workflow.md](workflow.md) §3.4a.

> **Not yet synthesised.**  The ax2 and gpu1 numbers above are SIM: correctness
> and cycle counts from Verilator.  Neither family has a SYNTH fit row yet, so
> no claim is made about whether a given tier fits the Tang Nano or the ULX3S.
> The tier targets named in the component titles are design intent, not measured
> fit.

## Functional coverage (board-independent, SIM)

These prove the RTL that every fitting configuration above inherits:

- Bare-metal: `make -C sw/baremetal check-hello check-timer check-preempt
  check-fencei check-spi check-sd`
- Accelerator roles: `check-role` (loopback), `check-tpu`, `check-gpu`,
  `check-gpu-perf`
- Lean-component suite: `check-suite-minimal` — `core.minimal` (the accelerator
  host in the GPU profiles) driving the CPU, GPU, and TPU in one run
- Superscalar-core suite: `make -C sim/unit run-suite-ax2` (every `core.ax2-*`
  tier against the official rv32ui + rv32um binaries on the RTL, at three
  wait-state settings) and `make -C sw/baremetal check-suite-ax2` (SoC
  integration: interrupts, fence.i, IPC, and the gpu1 role)
- Scalable-role suite: `make -C sim/unit run-suite-gpu1` (all four `role.gpu1-*`
  tiers against a C++ interpreter of the ISA, including maximal-bank-conflict
  and worst-case-serialisation kernels) and `make -C sw/baremetal check-gpu1`
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
