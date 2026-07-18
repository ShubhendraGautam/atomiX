# sw/ — all software

Everything compiled with the **stock RISC-V GNU toolchain**
(`riscv64-unknown-elf-gcc`, multilib; `-march=rv32i_zicsr -mabi=ilp32`,
later `rv32im_zicsr`) — no custom compiler work, industry ELF format
throughout.

| Subdirectory | Role | Runs on |
|---|---|---|
| [baremetal/](baremetal/) | crt0, linker scripts, bring-up programs | the target (no OS) |
| [kernel/](kernel/) | `aXos` — our monolithic kernel | the target |
| [user/](user/) | userland: init, sh, coreutils | the target, under aXos |
| [host/](host/) | `axhost` — host-side driver/daemon + role libraries | the host PC |

Three-platform rule (DESIGN.md §3.1): target software must run unchanged on
aXsim, QEMU `-machine virt`, and the RTL — never `#ifdef` per platform; if it
behaves differently somewhere, that's a bug in one of the platforms.
