# sim/testgen/ — random instruction-stream generator

`gen.py` is a small, own-code constrained generator chosen for phase 1. It
emits reproducible flat RV32I+Zicsr images that terminate through the standard
test finisher and run identically in aXsim and the Verilated core.

Streams cover ALU dependencies, aligned byte/half/word loads and stores,
forwarded branch/JAL redirects, `fence`/`fence.i`, and serialized CSR traffic.
Branches redirect to `PC+4`, so every generated stream is linear and its
length is deterministic while the redirect/flush path is still exercised.
Timing-counter CSR accesses are intentionally excluded: `mcycle` is a cycle
counter in RTL but instruction-based in the current ISS, an explicit model
difference that must be resolved before those accesses enter cosim fuzzing.

```bash
make -C sim/testgen quick     # one 10k-instruction stream with bus waits
make -C sim/testgen fuzz      # at least 10,000,000 checked cosim events
```

Every run prints its seed. A failing seed must be copied to `tests/directed/`
as a permanent regression before the underlying defect is fixed.
