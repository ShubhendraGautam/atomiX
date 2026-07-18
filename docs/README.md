# docs/ — block-level specifications

Per-block specs, split out of DESIGN.md as blocks solidify. DESIGN.md stays
the top-level contract; a spec lands here when a block needs more detail than
the contract should carry.

Expected documents (created in the phase that needs them):

- `axbus.md` — bus signals, timing diagrams, error semantics (phase 1)
- `csr-map.md` — implemented CSRs, reset values, WARL behavior (phase 1)
- `memory-map.md` — authoritative device map if it outgrows DESIGN.md §3.2
- `components.md` — selectable implementation model and extension boundary
- `host-protocol.md` — USB framing between `axhost` and the shell (phase 7)
- `role-interface.md` — doorbell, descriptor ring, role ID discovery (phase 7)

Convention: specs are written *before* the RTL/software that implements them,
and updated in the same change when behavior is deliberately altered.
