# atomiX

> **A DIY RISC-V computer, operating system, and FPGA platform.**
> Build the reference machine — or replace the parts that matter to you.

| Reference machine | Evidence | Platform direction |
|---|---|---|
| RV32IM, five stages, M/S/U + Sv32 | ISS · lock-step RTL · ISA tests · formal | ULX3S shell + future accelerator roles |

**Status:** simulation-verified reference system · component-first builds ·
physical ULX3S validation is the final evidence gate.

[Architecture](DESIGN.md) · [Build guide](docs/build.md) ·
[Dependencies](docs/dependencies.md) ·
[Live checklist](docs/design-checklist.md) ·
[Components](components/README.md)

---

## What is atomiX?

atomiX is a from-scratch RISC-V computer that grows into a reconfigurable FPGA
platform.  The reference build includes a five-stage CPU, SoC, bare-metal
runtime, and the aXos kernel.  The longer-term platform keeps that computer as
the management shell while accelerator roles attach at a defined boundary.

It is designed to be modified.  A user can substitute the CPU, memory,
interconnect, peripherals, board, simulation harness, or aXos service policy
without forking the rest of the project.

```text
  RISC-V core ── aXbus SoC ── aXos
       │             │          │
       └──── selectable components ────┐
                                        ▼
                    FPGA shell + future accelerator roles
```

## Start in three commands

Install the core tools first — the safe, tiered instructions are in
[Dependencies](docs/dependencies.md).

```bash
make -C sim/axsim test
make -C sw/baremetal images
make sim CONFIG=configs/sim-bram.json \
  RAM_INIT_FILE="$PWD/sw/baremetal/build/hello.hex"
```

That path builds the golden ISS, creates a target image, and runs it on the
selected Verilated SoC.  Continue with the [build guide](docs/build.md) for
three-platform checks, randomized cosimulation, aXos, formal verification,
and FPGA synthesis.

## Build it your way

Profiles select compatible components; manifests make every selection visible
and reproducible.

```bash
make component-list
make config-check CONFIG=configs/sim-bram.json
make component-show COMPONENT=memory.sdram
```

The stock integration contracts are intentionally small.  An external manifest
can point to an out-of-tree implementation, but it earns its own compatibility
and verification claim.  Read the [component catalog](components/README.md),
[profile guide](configs/README.md), and
[component map](docs/component-map.md) before making a replacement.

## Where to go next

| I want to… | Start here |
|---|---|
| Understand the machine | [DESIGN.md](DESIGN.md) |
| Build, test, or synthesize | [docs/build.md](docs/build.md) |
| Set up a host or FPGA toolchain | [docs/dependencies.md](docs/dependencies.md) |
| Change an implementation | [components/README.md](components/README.md) |
| Inspect current evidence and open work | [docs/design-checklist.md](docs/design-checklist.md) |
| Prepare the real board | [docs/ulx3s-bringup.md](docs/ulx3s-bringup.md) |

## Repository map

| Area | Purpose |
|---|---|
| [components/](components/) | Selectable manifests and their owned RTL/service sources |
| [configs/](configs/) | Reproducible system and kernel-service profiles |
| [sim/](sim/) | ISS, lock-step harnesses, SoC runner, and generators |
| [formal/](formal/) | riscv-formal and SymbiYosys integration |
| [rtl/](rtl/) | Generic FPGA flow and architecture entry points |
| [sw/](sw/) | Bare-metal runtime, boot ROM, aXos, and future host/user software |
| [docs/](docs/) | Build, dependency, architecture, and board documentation |

---

**Build what teaches. Verify what matters. Keep the seams open.**
