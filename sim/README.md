# sim/ — simulation and verification infrastructure

The non-synthesizable half of the project: the golden model, the lock-step
cosimulation rig, and test generation. This is where "correct" gets defined
and enforced (DESIGN.md §6).

| Subdirectory | Role |
|---|---|
| [axsim/](axsim/) | `aXsim` — our instruction-set simulator, the golden model |
| [cosim/](cosim/) | Verilator harness comparing RTL against aXsim per retired instruction |
| [testgen/](testgen/) | Random RISC-V instruction-stream generator for cosim fuzzing |

Three-platform rule: all software must run unchanged on aXsim, QEMU
(`-machine virt`), and the Verilated RTL — that's how "software bug" is
isolated from "hardware bug".
