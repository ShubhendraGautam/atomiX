# atomiX

atomiX is a from-scratch RISC-V computer and FPGA platform: a five-stage
RV32IM core, a small SoC, the aXos kernel, and a future shell-plus-accelerator
role system.  The project is deliberately DIY: users can replace an
implementation at the CPU, memory, interconnect, peripheral, board, harness,
or kernel-service boundary instead of treating the reference machine as a
black box.

The reference computer is verified in simulation through an ISS, lock-step
RTL cosimulation, ISA suites, randomized testing, and formal checks.  The
physical ULX3S proof is intentionally the final evidence gate while hardware
is unavailable.

## Start here

- [DESIGN.md](DESIGN.md) — architecture and design decisions.
- [Build and verification guide](docs/build.md) — the shortest reliable path
  to build, simulate, verify, and synthesize.
- [Dependencies and compatibility](docs/dependencies.md) — what each workflow
  needs, plus links to safe setup instructions.
- [Engineering checklist](docs/design-checklist.md) — current evidence,
  remaining platform work, and the final hardware gate.
- [Component catalog](components/README.md) and
  [configuration profiles](configs/README.md) — how to make a DIY variant.

## Component-first composition

The checked-in profiles select the verified reference implementation, but a
selection is not a lock-in.  Components own their implementation sources and
manifests; profiles compose them into a reproducible system.

```bash
make component-list
make config-check CONFIG=configs/sim-bram.json
make sim CONFIG=configs/sim-bram.json \
  RAM_INIT_FILE="$PWD/sw/baremetal/build/hello.hex"
```

An external manifest can select an out-of-tree implementation without copying
it into this repository.  The stock integration contracts are intentionally
small; a custom component earns its own compatibility and verification claim.
See [components/README.md](components/README.md) and
[docs/component-map.md](docs/component-map.md).

## Project map

| Path | Contents |
|---|---|
| [docs/](docs/) | Build, dependency, architecture, and board documentation |
| [components/](components/) | Selectable implementation manifests and their owned sources |
| [configs/](configs/) | Reproducible system and kernel-service profiles |
| [rtl/](rtl/) | Generic FPGA flow and architecture entry points; not a duplicate component source tree |
| [sim/](sim/) | ISS, lock-step harnesses, complete-SoC runner, and generators |
| [formal/](formal/) | riscv-formal and SymbiYosys integration |
| [sw/](sw/) | Bare-metal runtime, boot ROM, aXos, and future host/user software |
| [tests/](tests/) | RISC-V ISA-suite integration and directed regressions |

## Principles

- Build the CPU, bus, kernel, and role logic to learn; use established tools
  and standards everywhere else.
- Verify before hardware: simulation and formal evidence are first-class,
  while physical claims require physical evidence.
- Keep seams open: changing an implementation should not require forking the
  whole project.
