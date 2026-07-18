# atomiX documentation

`DESIGN.md` is the architectural contract.  This directory holds the focused
guides and interface specifications that make the contract buildable,
verifiable, and replaceable.

## Start with these guides

- [build.md](build.md) — profile selection, builds, verification, formal, and
  FPGA flow.
- [dependencies.md](dependencies.md) — dependency tiers and compatibility
  baseline.
- [design-checklist.md](design-checklist.md) — live, evidence-based status and
  the final hardware gate.
- [toolchain.md](toolchain.md) — exact Ubuntu/Debian setup and known tool
  workarounds.
- [ulx3s-bringup.md](ulx3s-bringup.md) — safe ULX3S board procedure.

## Architecture and composition

- [axbus.md](axbus.md) — the normative aXbus transaction contract.
- [memory.md](memory.md) — reference memory, cache, SDRAM, and SD architecture.
- [components.md](components.md) — component model and extension boundary.
- [component-map.md](component-map.md) — which repository areas are selectable
  and where their sources live.

## Planned specifications

The following documents are intentionally absent until their interface is
designed and an implementation is ready to consume it:

- `host-protocol.md` — host-link framing between `axhost` and the shell.
- `role-interface.md` — role MMIO, descriptors, discovery, and interrupts.

Keep a specification and its implementation change together whenever a
documented interface changes.
