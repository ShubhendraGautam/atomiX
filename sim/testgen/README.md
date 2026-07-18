# sim/testgen/ — random instruction-stream generator

`gen.py` is a small, own-code constrained generator. It emits reproducible
flat RV32IM+Zicsr images that terminate through the standard
test finisher and run identically in aXsim and the Verilated core.

Streams cover ALU dependencies, aligned byte/half/word loads and stores,
forwarded branch/JAL redirects, `fence`/`fence.i`, serialized CSR traffic,
and all eight RV32M multiply/divide operations.
Branches redirect to `PC+4`, so every generated stream is linear and its
length is deterministic while the redirect/flush path is still exercised.
Timing-counter CSR accesses are intentionally excluded: `mcycle` is a cycle
counter in RTL but instruction-based in the current ISS, an explicit model
difference that must be resolved before those accesses enter cosim fuzzing.

```bash
make -C sim/testgen quick     # one 10k-instruction stream with bus waits
make -C sim/testgen fuzz      # 10 seeds; target >=10,000,000 checked events
```

Every run prints its seed. A failing seed must be copied to `tests/directed/`
as a permanent regression before the underlying defect is fixed.

`run.py` scales the cosim watchdog with program length (eight cycles per
instruction by default, plus wait-state headroom). This is deliberately
bounded, but accommodates the fixed 32-cycle RV32M execution path; override
it with `--max-cycles-per-insn` when experimenting with another long-latency
unit.
