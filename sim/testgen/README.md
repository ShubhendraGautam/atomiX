# sim/testgen/ — random instruction-stream generator

Generates constrained-random RISC-V programs to fuzz the core through cosim —
the tests nobody thinks to write by hand: forwarding chains, back-to-back
hazards, traps landing on branch shadows, load-use against CSR serialization.

Approach (open question DESIGN.md §10.3, decide at phase 1): write a small
generator of our own vs adopt ideas from the industry-standard riscv-dv.
Either way the output contract is fixed: self-contained ELF images with a
known termination convention, runnable identically on aXsim and RTL.

Generations are seeded and reproducible; CI stores failing seeds as
regression tests in `tests/`.
