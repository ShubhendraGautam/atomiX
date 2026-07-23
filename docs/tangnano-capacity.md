# Tang Nano 20K capacity — what fits on the GW2A-18C

This is a measured fit study for the Sipeed Tang Nano 20K (Gowin
GW2AR-LV18QN88C8/I7, GW2A-18C family), not an estimate.  Numbers come from
`yosys synth_gowin -noabc9` on the profiles named below; they are pre-place-and-
route synthesis results (nextpnr packing will differ slightly but not change the
verdicts).  The device budget is roughly:

| Resource | GW2A-18C |
|---|---|
| LUT4 | 20,736 |
| Flip-flops | 15,552 |
| Block RAM (BSRAM, 18 Kbit) | ~46 blocks (828 Kbit) |
| DSP (18×18 multiplier) | ~48 |

## The verdict

The three shipped profiles are the CPU, the peaked GPU, and the TPU; the other
rows are measured points reachable by composing catalog components into a
profile.

| Configuration | LUT4 | Flip-flops | Block RAM | DSP | Fits? |
|---|---|---|---|---|---|
| **CPU** — `configs/tangnano20k.json` | ~11.3k | ~2.8k | 32 DPB | 0 | ✅ yes (32 KB RAM in BSRAM) |
| **GPU (peaked)** — `configs/tangnano20k-gpu.json` (minimal host + 6-lane) | ~20.2k | ~2k | 32 DPB | 6× MULT36X36 | ✅ yes, **tight** (97% LUT4) |
| **TPU** — `configs/tangnano20k-tpu.json` | 14.3k | 3.2k | 32 DPB + 8 DPX9B | 24× MULT9X9 | ✅ yes |
| 5-stage CPU + GPU 4-lane (`role.gpu-compute-lite`) | ~18.9k | ~6.2k | 32 DPB | 4× MULT36X36 | ✅ yes, comfortable |
| 5-stage CPU + GPU 8-lane (`role.gpu-compute`) | ~29.3k | ~5.3k | 48 DPB | 8× MULT36X36 | ❌ logic overflow (~1.4×) |
| CPU + GPU + TPU (all three) | — | — | — | — | ❌ unsupported — one role window |

**The GPU fits** because the engine is parameterized by `NLANES` (gpu_engine.sv):
`role.gpu-compute` is the 8-lane reference, `role.gpu-compute-lite` (4) and
`role.gpu-compute-6` (6) are the smaller variants.  Standalone engine cost scales
cleanly: 8-lane 12.6k LUT4 / 8 DSP, 6-lane ~9.5k / 6, 4-lane 6.6k / 4, 2-lane
3.7k / 2.  The GPU profile also drops main memory to 16 KB (`ram_bytes`, honoured
on the FPGA build via the board top's `RAM_BYTES` parameter) so main RAM (16 DPB)
+ the engine's 16 KB global buffer (16 DPB) sit inside the ~46-block BSRAM budget.
Functional equivalence and speed are covered by `make -C sw/baremetal check-gpu`
and `check-gpu-perf` (point `COMPONENT_CONFIG` at a composed profile to measure a
specific core/lane pairing).

## Why the profiles land where they do

**CPU (role.none) fits.**  The core, SoC, UART, CLINT, SPI, and boot ROM, with a
32 KB main memory that maps to 32 `DPB` block-RAM cells (the registered-read
`SYNC_READ=1` path — see [workflow.md](workflow.md#42-synthesis-only-gate-no-pr-tools-needed)).

**GPU (8-lane) is genuinely too much logic.**  The `role.gpu-compute` engine is
an 8-lane SIMT machine with a full 32-bit, multiply-capable ISA — ~29k LUT4 in
system.  It is now parameterized by `NLANES` (gpu_engine.sv), so the fix is to
build fewer lanes: the 4-lane and 6-lane variants above fit.

**TPU now fits after folding and RAM inference work.** `role.tpu-lite` computes
all eight output columns with 24 physical int8 MACs over three K phases
(3+3+2). Its 2,048-word C accumulator uses one mutually exclusive host/engine
port, so it infers BSRAM instead of 65,536 flip-flops. The result is 14.3k LUT
primitives, 3.2k FFs, and 24 `MULT9X9` cells with the same software interface.

## Accelerator-first: a minimal host to buy more GPU

The intuition is right — spend the fabric on the accelerator, not on a heavy
application CPU — but the measured lever is smaller than it looks.  `core.minimal`
is a compact multi-cycle RV32IM machine-mode core (no MMU, no S/U mode, no
pipeline/forwarding; it reuses the same decoder, ALU, mul/div, and register file
as the reference core).  Standalone on the Tang Nano it is **9.8k LUT4 versus
the 5-stage core's 11.3k — only ~14% smaller**, not the ~2× one might expect.

Why so little?  The Sv32 MMU is just ~1.2k LUT4 (two instances) and the pipeline
registers/forwarding are cheap; the real weight is the RV32IM **decode + CSR +
load/store and writeback muxing**, which a multi-cycle core still pays in full
(note the large `MUX2_LUT*` counts).  So the minimal host frees enough to move
the GPU from 4 lanes to 6, which is what the shipped `tangnano20k-gpu` profile
does — it lands at **20.2k of 20,736 LUT4 (97%)** — it synthesises and maps
(32 DPB, 6 DSP), but place-and-route would be tight and is unconfirmed without
the apicula tools.

What the swap *does* deliver is a better compute machine and a sharper
offload case: on the leaner (slower) host the on-core loop costs more, so GPU
offload wins by more — the `saxpy` benchmark speedup rises from ~1.4× (5-stage
host) to **~2.5×** (minimal host), and `poly` stays ~11×.  `core.minimal` hosts
the full accelerator flow; compose a `board.sim` profile with `core.minimal` and
a GPU role to run `check-gpu` / `check-gpu-perf` against it.

For a comfortable margin, pair `core.minimal` with the 4-lane GPU
(`role.gpu-compute-lite`); the shipped profile takes maximum GPU width (6 lanes),
which fits but leaves little headroom.

## Paths to make it "do tricks"

- **CPU + GPU** — ✅ done: `configs/tangnano20k-gpu.json` (minimal host + 6-lane),
  or `role.gpu-compute-lite` (4-lane) for a comfortable margin.
- **CPU + TPU on the 20K** — ✅ done: `configs/tangnano20k-tpu.json`, using the
  folded 24-MAC engine and BSRAM accumulator.
- **All three** — needs a larger part (e.g. ULX3S-85F or a bigger Gowin) plus a
  new composite role that hosts both engines behind the single role window.

## Reproducing these numbers

```bash
make -C rtl/fpga synth COMPONENT_CONFIG=$PWD/configs/tangnano20k-gpu.json BUILD=build-gpu
yosys -p "read_json build-gpu/tangnano20k_top.json; stat -top tangnano20k_top"
```
(Repeat with `tangnano20k-tpu.json`.)  On a memory-constrained host the final
mapping passes can thrash; `synth_gowin -noabc9 -run :map_cells; stat` gives the
block-RAM/DSP/flip-flop picture without the slowest pass.
