# Accelerator optimisation — design notes for discussion

Design-stage notes for the levers that would maximise the large FPGA target
(ULX3S-85F).  Nothing here is built yet; this is the material for a design
discussion before implementation.  Measured context and the delivered
optimisation (pipelined `LDX`) are in
[hardware-capabilities.md](hardware-capabilities.md).

Ranked by payoff:

| # | Lever | Payoff | Risk | Blocks / needs |
|---|---|---|---|---|
| 1 | Multi-bank GPU memory | high (breaks the lane-scaling ceiling) | high (correctness) | — |
| 2 | TPU `cbuf` → block RAM | frees ~65k FF | low–med (diagnosis) | — |
| 3 | Composite GPU + TPU role | run both accelerators at once | med | best after #2 |

---

## 1. Multi-bank GPU global memory

### Problem
`gmem` has one read/write port for the engine, so `LDX`/`STX` service one lane
per cycle.  Even with pipelined loads (1 cyc/lane), doubling lanes doubles the
per-wave memory time over half as many waves — memory time is flat, so past
~8 lanes more lanes barely help (measured ~1.35×/doubling).  The ceiling is
memory throughput, not lane count.

### Idea
Split `gmem` into **B interleaved banks** (bank = `addr[log2B-1:0]`, index =
`addr >> log2B`), each an independent block RAM.  A **coalesced** access — the
common SIMT pattern where lane L touches `base+L` — then hits B distinct banks
and can be serviced for all lanes in ~1 cycle (plus the read latency) instead
of N.

### The hard part: correctness under conflicts
The flat-global model lets any lane address any word (e.g. the `gather`/reverse
kernel: lane L reads `A[N-1-L]`), so a general **lane→bank crossbar** is
required, and bank conflicts (two lanes → same bank) must serialise.  Two
invariants the on-core oracle (`gpu.c`) enforces and any design must preserve:

- **Store order.** For a single `STX`, the per-instruction global-store order is
  (wave, instruction, ascending lane); if two lanes write the *same* address the
  higher lane wins.  Same address ⇒ same bank, so a bank that serialises its
  targeting lanes **lowest-lane-first across rounds** makes the highest lane the
  last writer — matching the oracle.  This must hold exactly.
- **Tail predication.** Lanes with `tid ≥ NTHREADS` are inactive: their stores
  are suppressed and their loads return 0.  The crossbar must carry the active
  mask per lane.

### Design options (to discuss)
- **Bank count B.** `B = NLANES` (best case 1-cycle coalesced, largest crossbar)
  vs `B = NLANES/2` or a fixed small B (cheaper crossbar, 2× serialisation on
  coalesced).  Crossbar cost ≈ B × NLANES routing + B priority-selects.
- **Full crossbar vs coalesced-only fast path.** A full crossbar handles any
  pattern.  A cheaper alternative detects the coalesced case (addresses are
  lane-consecutive) and takes a 1-cycle parallel path, falling back to today's
  serial path otherwise — much simpler, but only the coalesced kernels speed up.
- **Conflict sequencing.** Round-based: each round, every bank picks its
  lowest-index unserviced targeting lane (a priority encoder per bank), issues
  it, marks it served next cycle; repeat until the active set is empty.  Cycles
  = max bank occupancy (1 when coalesced, N worst case).

### Cost / verification
Sizeable RTL (crossbar, per-bank priority encoders, multi-cycle round FSM) and
correctness-sensitive.  Verification is the existing oracle at several lane
counts plus targeted conflict kernels (all-same-address store, strided/gather
loads).  **Open questions:** pick B; full crossbar vs coalesced fast-path first;
whether to bank the program/`A`/weight buffers too (probably not — only `gmem`).

---

## 2. TPU `cbuf` → block RAM

### Problem
The C accumulator (`cbuf`, 2048×32) maps to ~65k flip-flops, which is ~85% of
the ULX3S FF budget and the whole reason the TPU is "large".  Structurally
`cbuf` matches the GPU `gmem` (two ports, each write-or-registered-read), and
`gmem` **does** infer block RAM — so the cause is not obvious from the source.

### Approach
Diagnose before changing: synth `cbuf` in isolation and read the `memory_libmap`
decision (why it declined block RAM), then diff against `gmem`.  Candidate causes
to rule out:

- The drain read-modify-write under `ACC` (read `cbuf[idx]` then write it back a
  cycle later) may create a read/write dependency the mapper won't bank.
- A write-enable / read-enable expression difference from `gmem` (e.g. the
  `!busy_q` guard `gmem` has and `cbuf` does not).
- Index-expression or width detail that trips `memory_bram` for one and not the
  other.

Once the cause is known the fix is expected to be small (mirror the `gmem`
coding), like the `SYNC_READ` fix that moved the main memory from FFs to BSRAM.

### Correctness
Must preserve the drain semantics exactly: `C = acc_base + A×W`, `ACC`
read-modify-write accumulation across doorbells, and `RELU` clamp.  Verified by
`make -C sw/baremetal check-tpu` (three GEMMs vs the on-core reference).

**Open question:** is the RMW the blocker?  If so, does a small pipeline tweak
(separate read and write cycles per C element, which the drain already does)
suffice, or does the accumulator need a different port structure?

---

## 3. Composite GPU + TPU role

### Problem
The shell has a single role window (`0x4000_0000`, 64 KiB), so a profile picks
one accelerator.  The ULX3S has the LUT/DSP budget for both; with the `cbuf` fix
(#2) it has the FF budget too — so it could run CPU + GPU + TPU at once, which
the Tang Nano cannot.

### Idea
A composite role whose `axrole` module hosts both engines behind the one window,
split by a high offset bit (e.g. offset[15]: low half → GPU, high half → TPU),
muxing `rdata`/`ready`/`err` back.  Both accelerators already fit inside the
64 KiB window (GPU uses ≤0x5000, TPU ≤0x4000).

### Design options (to discuss)
- **Module naming.** Both role files define a module named `axrole`.  Reuse
  needs the engines as distinct modules — factor the TPU engine like the GPU
  already is (`gpu_engine`), so the composite instantiates `gpu_engine` +
  `tpu_engine` and provides the thin `axrole`.
- **Window layout & ID.** Give the composite a distinct `ROLE_ID` and publish
  both sub-window bases; the host driver discovers the composite, then drives the
  GPU and TPU sub-windows independently (they keep their own doorbell/status).
- **Shell vs role.** Alternatively teach the shell **multiple** role windows
  instead of a composite role — cleaner long-term, but a platform change (bus
  decode, kernel role driver) rather than a self-contained role.

### Cost / verification
Area ≈ GPU + TPU (fits the ULX3S per the measured parts, given #2).  Software: a
driver that runs a GPU job and a TPU job and checks both.  **Open question:**
composite role (self-contained, quick) vs multi-window shell (general, larger) —
this is the main thing to settle in the discussion.

---

## Not pursued (and why)

- **More lanes without banking** — measured diminishing returns (~1.35×/doubling)
  because of the single memory port; do #1 first.
- **Pipelining the control (overlap fetch/execute)** — turns the multi-cycle
  engine into a pipeline; large change, smaller payoff than memory bandwidth.
