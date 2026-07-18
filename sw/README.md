# sw/ — all software

Everything compiled with the **stock RISC-V GNU toolchain**
(`riscv64-unknown-elf-gcc`, multilib; `-march=rv32im -mabi=ilp32`) — no
custom compiler work, industry ELF format throughout. Newer toolchains may
use the explicit `rv32im_zicsr` spelling; see
[docs/dependencies.md](../docs/dependencies.md) for the GCC 10 compatibility
note.

| Subdirectory | Role | Runs on |
|---|---|---|
| [baremetal/](baremetal/) | crt0, linker scripts, bring-up programs | the target (no OS) |
| [kernel/](kernel/) | `aXos` — our monolithic kernel | the target |
| [user/](user/) | planned separately linked userland; current shell/demo live in aXos | the target, under aXos |
| [host/](host/) | planned `axhost` driver/daemon + role libraries | the host PC |

Three-platform rule (DESIGN.md §3.1): target software must run unchanged on
aXsim, QEMU `-machine virt`, and the RTL — never `#ifdef` per platform; if it
behaves differently somewhere, that's a bug in one of the platforms.

Build and verification commands are in [docs/build.md](../docs/build.md).
