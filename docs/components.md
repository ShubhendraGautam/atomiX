# Configurable component architecture

atomiX supports replacement at meaningful architectural seams while preserving
a simple route to the verified reference machine. The mechanism is intentionally
small: JSON manifests, standard-library Python, and generated Make variables.
There is no dependency solver, vendor framework, or custom HDL generator.

The catalog is in [`components/`](../components/README.md); supplied profiles
are in [`configs/`](../configs/). The resolver is:

```bash
python3 tools/configure.py list
python3 tools/configure.py resolve --config configs/sim-sdram.json
```

## What a configuration controls

A configuration selects `core`, `soc`, `memory`, `uart`, `clint`, `spi`,
`board`, and optionally `software` components. The supplied profiles select
the reference versions, but every one can name a built-in ID or an external
manifest file. Custom component kinds such as `gpio` are also preserved for a
custom SoC or harness. A profile may carry arbitrary `settings`; known
settings set RAM size, cache enable, and reset PC, while unknown settings are
exported rather than rejected.

```text
configuration JSON → configure.py → generated component-config.mk
                                      ├─ sim/soc Makefile
                                      └─ rtl/fpga Makefile
```

`make sim CONFIG=configs/sim-delayed.json ...` composes a Verilated SoC;
`make fpga CONFIG=configs/ulx3s-85f.json` composes the ECP5 flow. Existing
directory-level build commands continue to work with their reference defaults.
`make software CONFIG=configs/sim-axos.json` delegates to the selected
software component's own build target and then boots its resulting images.

## Reference adapters

`soc_top` now instantiates `axmem` rather than choosing BRAM, the delayed model,
or SDRAM itself. The default `axmem` delegates to `axmem_reference`, which
retains all prior behavior. A custom memory component can provide another
`axmem` module without modifying `soc_top`.

The same approach applies to the CPU and stock peripherals: a selected source
provides the module (`axcore`, `uart`, `clint`, or `axspi`) that the reference
SoC instantiates. The CPU boundary purposely excludes RVFI and rich trace
outputs; only the buses, interrupts, and minimal cache-maintenance trace are
connected. Users who need an incompatible interface should provide a custom
`soc_top` component instead of adding compatibility shims merely to satisfy a
catalog rule.

## Verification posture

Selection proves composition, not correctness. The `core.pipeline5` manifest
is the implementation covered by the existing ISA, cosimulation, and formal
claims. An alternative core can be selected and built immediately, but it earns
those claims only by running appropriate evidence. This makes experimentation
easy without weakening the meaning of the reference project's verification.

`configs/sim-finisher.json` is an executable proof of that distinction. It
selects `core.finisher-smoke`, a tiny core that writes the simulation pass value
and nothing else. It validates source replacement and the thin stock boundary;
it is intentionally not evidence for RISC-V execution.

The same posture applies to `software`: `software.axos-sdboot` is the known
kernel payload, not an imposed operating system. A custom software manifest
may build another kernel, monitor, or entirely bare-metal program from outside
this repository. The hardware composition layer only receives the images and
runner mode it declares.
