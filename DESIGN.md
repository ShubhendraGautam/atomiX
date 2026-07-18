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
| Languages | **Polyglot, right tool per layer**: C for target software (kernel, bare-metal, userland), C++ for host tooling (ISS/cosim — Verilator emits C++), Python for scripts | Cross-language conflicts are resolved at the boundary where they appear, case by case |
| ISA | RISC-V **RV32IM + Zicsr**, privileged spec M/S/U, **Sv32** MMU | Free GCC/LLVM/QEMU ecosystem; privileged spec is mandatory for the kernel goal |
| HDL | **SystemVerilog** (synthesizable subset supported by Yosys) | Verilator for fast sim; portable to any vendor flow |
| Microarchitecture | **Classic 5-stage pipeline from day one** (IF ID EX MEM WB) | Precise exceptions and hazard handling are designed in from the start, not retrofitted |
| Memory system | **BRAM first**, then delayed external-memory + I$/D$ caches and an x16 SDRAM controller | CPU↔memory already tolerates wait states, so the cache/controller slots in without core changes; physical proof is a board gate |
| Interconnect | **Custom minimal valid/ready bus**; Wishbone bridge later if we adopt third-party cores | We fully own and understand the "connectors" layer |
| Peripherals v1 | **UART console + CLINT (timer/software interrupts)**; PLIC, SD card, video later | Minimum viable for a preemptive kernel with a serial shell |
| FPGA target | **ULX3S v2/v3 85F (ECP5) / open flow** | Pin-constrained bitstream flow and SDRAM/UART PHY are checked in; P&R and physical proof remain explicit bring-up gates |
| Verification | **Own ISS golden model + lock-step cosim** in Verilator + **riscv-tests** + **riscv-formal** | Highest-confidence tier; the ISS doubles as a fast kernel-dev platform |
| Core memory ports | **Separate ibus + dbus masters** (Harvard at the core edge) | No structural hazards; caches later attach per-port with no core changes; SoC serves both from dual-port BRAM |
| Irregular instructions | **Serialize** CSR writes, `mret`, `fence.i` (later `div`): flush younger, complete alone, refetch | A few cycles on rare instructions buys away a whole class of in-flight side-effect hazards |
| Build order | **ISS first, then RTL** | RTL debugging starts with a trusted golden model and cosim from day one |
| Kernel | **Monolithic, xv6-inspired scope**, our own code | Achievable scope with a known-good reference for when we're stuck |
| Platform model | **Shell + role** (AWS F1 / Catapult style): aXcore + aXos fixed in every bitstream, role region swapped per mode | Kernel is genuinely common across modes; host driver never sees role internals, only the shell protocol |
| Component composition | **Manifest-selected implementations with lenient stock seams** | Users may substitute CPU, SoC fabric, memory, peripherals, board/harness, software/kernel code, or aXos service policies; manifests compose sources but do not prescribe microarchitecture or verification claims |
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
                 │  │  M/S/U modes, Sv32 MMU            │       │
                 │  │  optional I$ and D$ cache path    │       │
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
| `0x0010_0000` | 4 KB | Test finisher (QEMU `sifive_test`-compatible: `0x5555`=pass, `0x3333`\|code≪16=fail; simulation platforms only) |
| `0x0200_0000` | 64 KB | CLINT: `msip`, `mtimecmp`, `mtime` |
| `0x0C00_0000` | 4 MB | PLIC (reserved; implemented when we have >1 interrupt source) |
| `0x1000_0000` | 4 KB | UART0 (16550-compatible subset) |
| `0x1001_0000` | 4 KB | SPI0 (polling mode-0 controller for SD card) |
| `0x8000_0000` | 128 KB → 32 MB | RAM (BRAM in v1; 32 MiB x16 SDRAM on ULX3S). Kernel loads at `0x8000_0000` |

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

### 4.1 ISA profile and scope

The reference core implements RV32IM + Zicsr, precise synchronous traps,
machine/supervisor/user modes, CLINT interrupts, Sv32 page tables and TLB,
`sfence.vma`, SUM/MXR handling, and delegation through `medeleg`/`mideleg`.
Multiply and divide use a fixed-latency unit that stalls EX.

The C extension remains deliberately out of scope because it complicates
fetch alignment without enabling the current system goals.  The A extension is
also out of scope for the single-hart design; kernel critical sections disable
interrupts.  Revisit either only when a concrete enabling need exists.

The portable baseline is `-march=rv32im -mabi=ilp32`.  Newer toolchains may
use the explicit `rv32im_zicsr` spelling; Ubuntu 22.04 GCC 10 accepts CSR
instructions through the compatible `rv32im` spelling.

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
- **`FENCE`:** no extra hardware action in the single-hart write-through
  cache design. **`FENCE.I`:** serializes in the core and, when the selected
  profile enables caches, retires a registered I-cache invalidation before
  refetch.
  **`WFI`:** executes as a nop in v1.

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

The recommended automation policy is to run simulation legs on every relevant
change and formal jobs on core/RVFI changes.  The reproducible commands and
current evidence are in [docs/build.md](docs/build.md) and
[docs/design-checklist.md](docs/design-checklist.md).

## 7. Software stack

- **Toolchain:** stock `riscv64-unknown-elf-gcc` (multilib rv32) — no custom
  compiler work.
- **Bare metal:** crt0, linker script, MMIO helpers, timer/preemption, and SD
  bring-up programs are checked across ISS, QEMU, and RTL.
- **Kernel (`aXos`):** monolithic and xv6-inspired in scope: Sv32, trap
  handling, tasks, selectable scheduling/VM/storage policies, a resident
  shell, and SD boot/storage paths.  It is developed against aXsim/QEMU in
  parallel with RTL under the platform-compatibility rule (§3.1).
- **Future user and host software:** separately linked userland and `axhost`
  begin only when their executable-loader and host-link contracts are defined.

## 8. Engineering status and next work

The reference CPU, SoC, kernel, memory/storage path, component composition,
and simulation/formal verification infrastructure are complete to their
current contracts.  The live, command-backed status is maintained in
[docs/design-checklist.md](docs/design-checklist.md), rather than duplicating
a phase ledger here.

The next platform work is the host-link/role contract and its loopback proof,
followed by the TPU-lite role.  ECP5 place-and-route and physical ULX3S
bring-up remain the final gate: they do not block simulation or component work,
but no physical-hardware claim is made before their evidence is recorded.

## 9. Repository layout

```
atomiX/
├── DESIGN.md            # this document
├── docs/                # per-block specs as they solidify (bus, CSR map, …)
├── components/          # selectable manifests and owned RTL/service sources
├── configs/             # reproducible component selections for sim/boards
├── tools/               # dependency-free configuration resolver
├── rtl/
│   ├── core/            # CPU architecture signpost -> components/core/
│   ├── soc/             # SoC architecture signpost -> component owners
│   ├── roles/           # future role design area
│   └── fpga/            # generic ECP5 flow; board sources live in components/
├── sim/
│   ├── axsim/           # the ISS golden model
│   ├── cosim/           # Verilator harness, lock-step checker
│   ├── soc/             # generic complete-SoC runner
│   └── testgen/         # random instruction generator
├── formal/              # riscv-formal glue + SymbiYosys configs
├── sw/
│   ├── baremetal/       # crt0, linker scripts, bring-up programs
│   ├── kernel/          # aXos orchestration; services selected from components/
│   ├── user/            # future separately linked userland
│   └── host/            # future axhost driver/daemon + role libraries
└── tests/               # riscv-tests submodule + directed tests
```

## 10. Deferred design decisions

1. **License:** choose the repository license before external distribution.
2. **Host link:** define USB-serial framing, flow control, failure recovery,
   and whether bitstream upload shares the transport or uses the FTDI JTAG
   path.
3. **Role interface:** allocate the role MMIO region and descriptor format,
   including doorbell and completion semantics (polling versus PLIC).
4. **TPU-lite sizing:** choose array dimensions within the ECP5-85F DSP budget
   and settle the int8×int8→int32 accumulation/activation split.
5. **UART compatibility depth:** retain the current 16550-style subset or
   expand it only when a concrete software compatibility need appears.

Physical-board observations are not an open design question: they are the
separate final evidence gate in [docs/design-checklist.md](docs/design-checklist.md).
