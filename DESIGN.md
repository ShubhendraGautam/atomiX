# atomiX — Design Document

A computer system built from scratch — CPU → SoC → kernel → OS — that grows
into a **reconfigurable FPGA accelerator platform**: one FPGA that can serve as
a CPU, a TPU-style matrix engine, or other roles, managed by our own kernel and
controlled from a host PC through our own driver. This document records the
closed design decisions and the phased plan. It is the contract for everything
we build.

## 1. Goals and non-goals

**Goals**

1. A RISC-V CPU and surrounding SoC (memory, bus, peripherals) written by us in
   synthesizable SystemVerilog.
2. Our own monolithic Unix-like kernel (xv6-inspired *scope*, not a copy) running
   on that SoC: processes, virtual memory, syscalls, a filesystem, a shell.
3. FPGA-portable from day one: every line of RTL obeys FPGA constraints
   (synchronous single-clock design, BRAM-shaped memories, no latches), targeting
   the open Yosys + nextpnr flow for Lattice ECP5.
4. "As close to actually working as possible": verified against a golden model,
   the official ISA tests, and formal proofs — not just demos that happen to run.
5. **A shell + role accelerator platform** (§3.3): the CPU + kernel become the
   permanent management plane ("shell") of the FPGA; swappable "roles" (first: a
   TPU-lite systolic array) attach as aXbus devices; a host-side driver controls
   the whole card over a host link. Goals 1–4 are unchanged — they *are* the
   shell.

**Non-goals (for now)**

- Multicore / cache coherency.
- Performance competitiveness — correctness and clarity win every tie.
- USB, Ethernet, or graphics in v1 (the v1 machine is headless over UART).
- ASIC considerations.

## 2. Decision record

| Decision | Choice | Key consequence |
|---|---|---|
| Build vs adopt | **Scratch-build only what teaches** (core, bus, kernel, roles); **adopt the industry standard everywhere else** (RISC-V ISA, stock GCC, ELF, riscv-tests, riscv-formal, Verilator, QEMU-`virt` map, 16550 UART, xv6 scope, Wishbone-adjacent bus) | Maximum support and knowledge base; our effort concentrates where the learning is |
| ISA | RISC-V **RV32I + Zicsr**, privileged spec M/S/U, **Sv32** MMU; add **M** ext. in phase 2 | Free GCC/LLVM/QEMU ecosystem; privileged spec is mandatory for the kernel goal |
| HDL | **SystemVerilog** (synthesizable subset supported by Yosys) | Verilator for fast sim; portable to any vendor flow |
| Microarchitecture | **Classic 5-stage pipeline from day one** (IF ID EX MEM WB) | Precise exceptions and hazard handling are designed in from the start, not retrofitted |
| Memory system | **BRAM first**, SDRAM + I$/D$ caches in a later phase | Early phases have simple 1-cycle memory; CPU↔memory interface must already tolerate wait states so caches slot in without CPU changes |
| Interconnect | **Custom minimal valid/ready bus**; Wishbone bridge later if we adopt third-party cores | We fully own and understand the "connectors" layer |
| Peripherals v1 | **UART console + CLINT (timer/software interrupts)**; PLIC, SD card, video later | Minimum viable for a preemptive kernel with a serial shell |
| FPGA target | **ECP5 / open flow**, but **simulation-only for now** — RTL designed against a virtual ULX3S-like target | No board purchase yet; timing closure risk deferred deliberately |
| Verification | **Own ISS golden model + lock-step cosim** in Verilator + **riscv-tests** + **riscv-formal** | Highest-confidence tier; the ISS doubles as a fast kernel-dev platform |
| Core memory ports | **Separate ibus + dbus masters** (Harvard at the core edge) | No structural hazards; caches later attach per-port with no core changes; SoC serves both from dual-port BRAM |
| Irregular instructions | **Serialize** CSR writes, `mret`, `fence.i` (later `div`): flush younger, complete alone, refetch | A few cycles on rare instructions buys away a whole class of in-flight side-effect hazards |
| Build order | **ISS first, then RTL** | RTL debugging starts with a trusted golden model and cosim from day one |
| Kernel | **Monolithic, xv6-inspired scope**, our own code | Achievable scope with a known-good reference for when we're stuck |
| Platform model | **Shell + role** (AWS F1 / Catapult style): aXcore + aXos fixed in every bitstream, role region swapped per mode | Kernel is genuinely common across modes; host driver never sees role internals, only the shell protocol |
| Mode switching | **Full bitstream swap** (host uploads new bitstream, FPGA reboots) | Partial reconfiguration is effectively unsupported in the open Yosys/nextpnr flow; full swap is simple and reliable |
| Host link | **USB** — FTDI USB-serial first (~1–3 MB/s), soft USB device core later | Zero extra hardware on ULX3S-class boards; models as a virtual pipe in simulation |
| Role interface | **aXbus MMIO device with doorbell + descriptor ring** | Same idiom as real NVMe/GPU hardware; one driver model for every role |
| First role | **TPU-lite: int8 systolic GEMM array** on ECP5 DSP blocks | Most tractable "real" accelerator; clearly benchmarkable against host matmul |

## 3. System architecture

```
                 ┌─────────────────────────────────────────────┐
                 │                  atomiX SoC                  │
                 │                                             │
   ┌─────────┐  │  ┌───────────────────────────────────────┐  │
   │  UART   │◄─┼─►│            aXbus interconnect          │  │
   │ (host   │  │  │      (valid/ready, 32-bit, 1 master    │  │
   │ terminal│  │  │       now, arbiter-ready for DMA)      │  │
   └─────────┘  │  └───┬───────────┬───────────┬───────────┘  │
                 │      │           │           │              │
                 │  ┌───┴────┐ ┌────┴────┐ ┌────┴────┐         │
                 │  │ CLINT  │ │ Boot ROM│ │  RAM    │         │
                 │  │ mtime, │ │ (BRAM)  │ │ (BRAM → │         │
                 │  │ msip   │ │         │ │  SDRAM) │         │
                 │  └───▲────┘ └─────────┘ └─────────┘         │
                 │      │ timer/sw irq                         │
                 │  ┌───┴──────────────────────────────┐       │
                 │  │        aXcore CPU                 │       │
                 │  │  RV32I(M) Zicsr, 5-stage pipeline │       │
                 │  │  M/S/U modes, Sv32 MMU (phase 4)  │       │
                 │  │  [I$ and D$ added in phase 6]     │       │
                 │  └───────────────────────────────────┘       │
                 └─────────────────────────────────────────────┘
```

### 3.1 Platform compatibility rule

The memory map and peripheral programming models follow **QEMU's `virt`
machine** wherever we don't have a reason to differ. Payoff: every piece of
software we write (bare-metal tests, the kernel) runs on three platforms with
zero changes — our ISS, QEMU, and our RTL — which is how we isolate "software
bug" from "hardware bug".

### 3.2 Memory map (v1)

| Base | Size | Device |
|---|---|---|
| `0x0000_1000` | 4 KB | Boot ROM (BRAM, `$readmemh`-initialized) |
| `0x0200_0000` | 64 KB | CLINT: `msip`, `mtimecmp`, `mtime` |
| `0x0C00_0000` | 4 MB | PLIC (reserved; implemented when we have >1 interrupt source) |
| `0x1000_0000` | 4 KB | UART0 (16550-compatible subset) |
| `0x8000_0000` | 128 KB → 32 MB | RAM (BRAM in v1; SDRAM later). Kernel loads at `0x8000_0000` |

Misaligned or unmapped accesses raise the appropriate precise exception; the
bus returns an error response rather than hanging.

Role MMIO and role-visible RAM windows will be assigned from unused space
(candidate: `0x4000_0000` region) when the role framework is specified.

### 3.3 Shell + role platform model

The endgame architecture. The FPGA design is split into two parts:

```
┌────────────────────── FPGA ──────────────────────┐
│  SHELL (identical RTL in every bitstream)        │
│  ┌────────┐ ┌──────┐ ┌───────────┐ ┌──────────┐  │
│  │ aXcore │ │ SDRAM│ │ host link │ │ UART/SD  │  │
│  │ + aXos │ │ ctrl │ │   (USB)   │ │          │  │
│  └───┬────┘ └──┬───┘ └─────┬─────┘ └────┬─────┘  │
│      └───── aXbus ─────────┴────────────┘        │
│                 │                                │
│  ┌──────────────┴───────────────────────────┐    │
│  │  ROLE (differs per bitstream)            │    │
│  │  none/extra-CPU │ TPU (systolic) │ ...   │    │
│  └──────────────────────────────────────────┘    │
└──────────────────────────────────────────────────┘
         ▲ USB
┌────────┴─────────┐
│ Host PC (Linux)  │  axhost driver/daemon: upload bitstream (mode switch),
│                  │  submit work, move buffers — via the shell protocol only
└──────────────────┘
```

- **Shell** = aXcore + aXos + aXbus + memory controller + host link + boot.
  Same source, present in every bitstream. This is where "the ISA is common"
  and "the kernel is common" are literally true: aXos always runs on the
  shell's RISC-V core, in every mode.
- **Role** = the mode-specific accelerator, attached to aXbus as an MMIO
  device: a doorbell register, a descriptor ring in RAM, an interrupt line
  (via PLIC when it exists). aXos discovers the role via an ID register,
  feeds it work, and exposes it over the host link. A role does **not**
  execute RISC-V; it consumes descriptors.
- **Mode switch** = the host uploads a different full bitstream and the FPGA
  reboots (~1 s on ECP5). "Shell is fixed" means fixed at the source level —
  one build per role, same shell RTL in each.
- **Host driver (`axhost`)** = userspace tool/daemon on the host PC speaking a
  small framed protocol over USB: bitstream upload, buffer read/write, work
  submission, completion events. It knows the shell protocol, never the role
  internals — role-specific logic lives in aXos and in per-role host libraries
  above `axhost`.
- **First role: TPU-lite** — an int8 systolic GEMM array (target ~8×8 MACs,
  sized to ECP5 DSP-block budget), with weight-stationary dataflow and an
  accumulate/activation output stage. Chosen because a systolic array is the
  most tractable genuinely-real accelerator and offloaded matmul is trivially
  benchmarkable against the host CPU.

Simulation story is unchanged: the host link models as a virtual pipe, so the
full stack — axhost on the real host, aXos on the simulated shell, role RTL —
runs end-to-end under Verilator before any hardware exists.

## 4. CPU: `aXcore`

### 4.1 ISA subset by phase

- **Phase 1:** RV32I + Zicsr + `ecall`/`ebreak`/`mret`, machine mode only,
  full trap support (illegal instruction, misaligned, ecall, external/timer/
  software interrupts).
- **Phase 2:** M extension (multiply/divide; multi-cycle unit that stalls EX).
- **Phase 4:** S and U modes, `sret`, Sv32 page tables, `sfence.vma`, TLB,
  `mstatus` interposition (SUM/MXR), delegation via `medeleg`/`mideleg`.
- **Deliberately skipped:** C extension (complicates fetch alignment for little
  design value here), A extension (single hart; kernel critical sections use
  interrupt disabling). Revisit A only if multicore ever enters scope.

Toolchain flags: `-march=rv32i_zicsr -mabi=ilp32` (later `rv32im_zicsr`).

### 4.2 Pipeline

Classic 5 stages: **IF → ID → EX → MEM → WB**.

- **Hazards:** full forwarding (EX/MEM and MEM/WB → EX); one-cycle load-use
  stall; branches resolved in EX with static not-taken prediction (2-cycle
  taken-branch penalty). No branch predictor in v1 — the interface leaves room
  for one later.
- **Stalls:** IF and MEM issue requests on the bus with valid/ready; any
  wait-state stalls the pipeline upstream. This is the provision that lets
  BRAM (1-cycle) be swapped for caches+SDRAM (variable) without touching the
  core.
- **Precise exceptions — designed in, not bolted on:** every instruction
  carries its PC and an exception tag down the pipeline. Faults mark the
  instruction and travel to a single commit point (MEM/WB boundary) where the
  trap is taken: younger in-flight instructions are flushed, `mepc`/`mcause`/
  `mtval` are written, and fetch redirects to `mtvec`. Interrupts are injected
  at the same commit point so they are precise too.
- **Memory ports:** the core exposes two independent aXbus masters — `ibus`
  (fetch, from IF) and `dbus` (loads/stores, from MEM). Harvard at the core
  edge, unified behind it: the v1 SoC serves both from dual-port BRAM; later,
  I$ and D$ attach one per port with no core changes. Fetch and data access
  never contend, so the pipeline has no structural hazards.
- **Register file:** 32×32 flip-flops, x0 hardwired to zero, 2 read ports +
  1 write port, with an internal write-before-read bypass so an instruction
  in ID sees the value WB writes in the same cycle (the forwarding path
  people forget).
- **Irregular instructions are serialized:** CSR writes, `mret`, and `fence.i`
  (later: divide) execute alone — younger instructions are flushed, the
  instruction completes, fetch restarts after it. Rare-instruction cycles
  traded for the elimination of in-flight side-effect hazards; no CSR
  forwarding network exists or needs verifying.
- **CSR file:** its own module, accessed in EX under the serialization rule.
- **`FENCE`/`FENCE.I`:** architecturally correct no-ops until caches exist
  (single hart, uncached); `fence.i` keeps its serializing flush so it is
  already correct when an I$ appears. **`WFI`:** executes as a nop in v1.

### 4.3 Correctness definition

The core is correct when it (a) passes all rv32ui/rv32mi/rv32si riscv-tests,
(b) retires lock-step-identical to the ISS over long randomized programs, and
(c) passes riscv-formal's bounded checks. All three, not any one.

## 5. Interconnect: `aXbus`

Minimal synchronous request/response bus, single outstanding transaction:

```
master → slave:  valid, addr[31:0], wdata[31:0], wstrb[3:0] (0000 = read)
slave  → master: ready, rdata[31:0], err
```

- Transaction completes on `valid && ready`. Slaves may hold `ready` low
  (wait states) — CPU stalls, which is exactly the cache/SDRAM provision.
- `err` on decode miss or slave fault → precise access-fault exception.
- Address decode in a top-level `aXbus_mux`; one master today, arbiter slot
  reserved for a future DMA/debug master.
- **Wishbone posture:** aXbus is deliberately a near-subset of Wishbone
  classic; a bridge is a thin adapter if/when we import third-party cores.

## 6. Verification strategy (three legs)

1. **`aXsim` — our own RV32 ISS** (C++ or Rust — see open questions), written
   *first*, before any RTL. Instruction-accurate, models the same memory map,
   CSRs, and Sv32. Runs riscv-tests itself to establish trust. Doubles as the
   fast kernel-development platform.
2. **Lock-step cosimulation:** Verilator wraps the RTL; on every retired
   instruction the testbench compares (PC, instruction, rd write, CSR effects,
   trap taken) against `aXsim`. Divergence dumps waveform + ISS trace at the
   exact instruction. Fed by riscv-tests, directed tests, and a random
   instruction generator.
3. **riscv-formal + SymbiYosys:** bounded formal proofs of the pipeline
   (register writeback correctness, PC ordering, trap precision).

CI runs legs 1–2 on every change; formal runs on core changes.

## 7. Software stack plan

- **Toolchain:** stock `riscv64-unknown-elf-gcc` (multilib rv32) — no custom
  compiler work.
- **Bare-metal phase:** crt0 + linker script + tiny libc subset (`printf` over
  UART) for bring-up programs and tests.
- **Kernel (`aXos`):** monolithic, xv6-inspired scope, our own code:
  boot → paging on (Sv32) → trap handling → processes + round-robin scheduler
  (preemption via CLINT timer) → fork/exec/exit/wait/read/write/pipe →
  simple inode filesystem (RAM-disk first, SD card later) → userland: init,
  `sh`, `ls`, `cat`, `echo`.
- Kernel development starts on `aXsim`/QEMU in parallel with RTL work — the
  platform-compatibility rule (§3.1) makes it land on real RTL unchanged.

## 8. Roadmap

| Phase | Deliverable | Exit criterion |
|---|---|---|
| **0. Foundations** | Repo layout, toolchain container/Makefiles, `aXsim` ISS | ISS passes rv32ui + rv32mi riscv-tests |
| **1. Core, M-mode** | 5-stage RV32I+Zicsr core, traps, lock-step cosim rig | RTL passes riscv-tests + 10⁷-instruction random cosim with zero divergence |
| **2. Formal + M ext** | riscv-formal integration; multiply/divide unit | Formal checks pass; `rv32um` tests pass |
| **3. SoC v1** | aXbus, BRAM ROM/RAM, UART, CLINT; interrupt-driven bare-metal demo | Timer-preempted multitasking demo over UART, identical on ISS/QEMU/RTL |
| **4. Privileged CPU** | S/U modes, Sv32 MMU + TLB, delegation | rv32si tests + cosim with paging-heavy random tests |
| **5. Kernel bring-up** | `aXos` core: paging, processes, syscalls, shell on RAM-disk | Interactive shell on the RTL simulation console |
| **6. Real memory** | SDRAM controller + I$/D$ (board-dependent), SD card (SPI) + on-disk FS | Kernel boots from SD, runs in SDRAM |
| **7. FPGA bring-up** | Board purchase decision, pinout/constraints, timing closure | Same bitstream-driven shell on physical hardware |
| **8. Host link + role framework** | USB framed protocol, `axhost` host tool, role slot on aXbus (doorbell + descriptor ring), null "loopback" role | Host submits a buffer, loopback role echoes it, completion reaches `axhost` — end-to-end in sim and on hardware |
| **9. TPU-lite role** | int8 systolic GEMM array + aXos role service + host matmul library | Offloaded matmul matches reference results; measured speedup vs aXcore software matmul |
| **10. Platform polish** | Bitstream-swap mode switching from `axhost`, second role (stretch), docs | One-command mode switch; same `axhost` drives both roles |

Phases 0–5 need no hardware purchase and are unchanged by the platform pivot —
they build the shell. Phase 8's framework can be developed entirely in
simulation (virtual-pipe host link). The board decision (ULX3S is the standing
favorite) is deferred to phase 6/7 — revisit availability then.

## 9. Repository layout

```
atomiX/
├── DESIGN.md            # this document
├── docs/                # per-block specs as they solidify (bus, CSR map, …)
├── rtl/
│   ├── core/            # aXcore: pipeline stages, CSR file, MMU
│   ├── soc/             # aXbus, memories, UART, CLINT, top-level (the shell)
│   ├── roles/           # role designs: loopback, tpu/ (phase 8+)
│   └── fpga/            # board top-levels + constraints (phase 7)
├── sim/
│   ├── axsim/           # the ISS golden model
│   ├── cosim/           # Verilator harness, lock-step checker
│   └── testgen/         # random instruction generator
├── formal/              # riscv-formal glue + SymbiYosys configs
├── sw/
│   ├── baremetal/       # crt0, linker scripts, bring-up programs
│   ├── kernel/          # aXos (incl. role services, host-link protocol)
│   ├── user/            # userland programs
│   └── host/            # axhost driver/daemon + per-role host libraries
└── tests/               # riscv-tests submodule + directed tests
```

## 10. Open questions (to close in phase 0)

1. **ISS language:** C++ (easier Verilator linkage for cosim) vs Rust
   (safety, nicer tooling). Leaning C++ for the cosim boundary.
2. **License** for the repo (BSD/MIT/Apache-2.0).
3. **Random test generation:** write our own generator vs adopt
   riscv-dv-lite approaches — decide when phase 1 cosim starts.
4. **UART model:** full 16550 register compatibility vs minimal
   data/status pair. QEMU-`virt` alignment argues for the 16550 subset.

**To close in phase 8 (platform):**

5. **Host-link protocol:** framing, flow control, error recovery over
   USB-serial; whether bitstream upload goes through the same channel or
   the board's FTDI JTAG path.
6. **Role MMIO map + descriptor format:** ring layout, doorbell semantics,
   completion signaling (polling vs PLIC interrupt).
7. **TPU-lite dimensions:** array size vs ECP5-85F DSP budget (156 18×18
   multipliers), int8×int8→int32 accumulate, activation ops in HW vs on
   aXcore.
