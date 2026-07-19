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

| Profile | LUT4 | Flip-flops | Block RAM | DSP | Fits? |
|---|---|---|---|---|---|
| `configs/tangnano20k.json` — **CPU** | ~11.3k | ~2.8k | 32 DPB | 0 | ✅ yes (synthesises; 32 KB RAM in BSRAM) |
| `configs/tangnano20k-gpu-lite.json` — **CPU + GPU (4-lane)** | ~18.9k | ~6.2k | 32 DPB | 4× MULT36X36 | ✅ **yes** (16 KB RAM; the fitting GPU build) |
| `configs/tangnano20k-gpu.json` — **CPU + GPU (8-lane)** | ~29.3k | ~5.3k | 48 DPB | 8× MULT36X36 | ❌ logic overflow (~1.4× LUT4) |
| `configs/tangnano20k-tpu.json` — **CPU+TPU** | — | ~69.5k | 32 DPB | 64× MULT9X9 | ❌ FF overflow (~4.5×) |
| `configs/tangnano20k-mini-gpu6.json` — **minimal CPU + GPU (6-lane)** | ~20.2k | ~2k | 32 DPB | 6× MULT36X36 | ✅ yes, **tight** (97% LUT4) |
| CPU + GPU + TPU (all three) | — | — | — | — | ❌ impossible — a single accelerator already overflows, and the shell has one role window |

**The GPU now fits** at 4 lanes.  The engine is parameterized by `NLANES`
(gpu_engine.sv); `role.gpu-compute` is the reference 8-lane wrapper and
`role.gpu-compute-lite` is the 4-lane variant for small parts.  Standalone
engine cost scales cleanly with lanes: 8-lane 12.6k LUT4 / 8 DSP, 4-lane 6.6k /
4, 2-lane 3.7k / 2.  The `tangnano20k-gpu-lite` profile also drops main memory
to 16 KB (`ram_bytes`, honoured on the FPGA build via the board top's `RAM_BYTES`
parameter) so main RAM (16 DPB) + the engine's 16 KB global buffer (16 DPB) sit
inside the ~46-block BSRAM budget.  Functional equivalence and speed are covered
by `make -C sw/baremetal check-gpu` (4-lane sim) and the `check-gpu-perf` /
`check-gpu-perf-lite` performance regressions.

## Why each fails — they are different problems

**CPU (role.none) fits.**  The core, SoC, UART, CLINT, SPI, and boot ROM, with a
32 KB main memory that maps to 32 `DPB` block-RAM cells (the registered-read
`SYNC_READ=1` path — see [workflow.md](workflow.md#42-synthesis-only-gate-no-pr-tools-needed)).

**GPU (8-lane) is genuinely too much logic.**  The `role.gpu-compute` engine is
an 8-lane SIMT machine with a full 32-bit, multiply-capable ISA — ~29k LUT4 in
system.  It is now parameterized by `NLANES` (gpu_engine.sv), so the fix is to
build fewer lanes: the 4-lane and 6-lane variants above fit.

**TPU is a fixable block-RAM gap, not raw size.**  The `role.tpu-lite` C
accumulator (`cbuf`, 2048×32) falls back to 65,536 flip-flops instead of block
RAM.  It is a two-write-port read-modify-write accumulator; unlike the GPU's
`gmem` (which maps to 16 `DPB`), it does not infer BSRAM.  Making `cbuf` block-
RAM-friendly drops the flip-flop count ~20×; after that the fit hinges on the
64 `MULT9X9` multipliers versus ~48 DSP blocks (so the 8×8 array may also need
to shrink).  This is the same class of issue that the main memory had before the
`SYNC_READ` fix.

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
the GPU from 4 lanes to 6, but the result (`tangnano20k-mini-gpu6`) lands at
**20.2k of 20,736 LUT4 (97%)** — it synthesises and maps (32 DPB, 6 DSP), but
place-and-route would be tight and is unconfirmed without the apicula tools.

What the swap *does* deliver is a better compute machine and a sharper
offload case: on the leaner (slower) host the on-core loop costs more, so GPU
offload wins by more — the `saxpy` benchmark speedup rises from ~1.4× (5-stage
host) to **~2.5×** (minimal host), and `poly` stays ~11×.  `core.minimal` hosts
the full accelerator flow: `make -C sw/baremetal check-gpu` and the
`check-gpu-perf` regressions pass on the `sim-minimal*` profiles.

For a comfortable margin, pair `core.minimal` with the 4-lane GPU; for maximum
GPU width on this part, the 6-lane pairing fits but leaves little headroom.

## Paths to make it "do tricks"

- **CPU + GPU (4-lane)** — ✅ done: `configs/tangnano20k-gpu-lite.json`
  (role.gpu-compute-lite).  A programmable SIMT engine that fits with headroom.
- **CPU + TPU on the 20K** — fix `cbuf` to infer BSRAM, then shrink the systolic
  array if the DSP count overflows.  Still open.
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
