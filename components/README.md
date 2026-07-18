# atomiX component catalog

This directory makes atomiX a DIY platform rather than a collection of
hard-wired implementation choices. Each `kind/implementation/component.json`
file describes a selectable implementation. Built-in RTL, harness, board, and
aXos-service assets live beside their owning component; an external manifest
may instead keep its sources in its own portable folder. Software components
remain project roots because their build contract includes startup code,
linker scripts, and produced images. The manifest, not a particular build
directory, is the integration boundary.

The built-in selections are deliberately modest: the verified five-stage CPU,
reference SoC shell, three reference memory modes, standard peripherals,
simulation/ULX3S boards and harnesses, SoC infrastructure, and aXos service
policies.
`core.finisher-smoke` is a deliberately tiny,
non-RISC-V composition example: it proves an alternate CPU source can replace
the reference core, but is explicitly not a software-compatible CPU.

## Everyday use

```bash
make component-list
make component-show COMPONENT=memory.sdram
make config-check CONFIG=configs/sim-sdram.json
make sim CONFIG=configs/sim-bram.json \
  RAM_INIT_FILE="$PWD/sw/baremetal/build/hello.hex"
make fpga CONFIG=configs/ulx3s-85f.json
make -C sw/kernel kernel-config KERNEL_CONFIG=../../configs/kernel-cooperative.json
make component-show COMPONENT=cache.passthrough
```

`configs/` contains ready-to-run profiles. `sim-bram`, `sim-delayed`, and
`sim-sdram` select the same SoC with progressively more realistic backing
memory. `ulx3s-85f` selects the ECP5 board target.

## Lenient contracts

Manifests do **not** enumerate an ISA, pipeline structure, cache geometry,
memory policy, or verification status as a condition of selection. Those are
descriptive capabilities only. `tools/configure.py` validates component IDs and
source paths, but accepts partial or non-stock compositions too. It also
passes through custom component kinds (for example `gpio` or `debug_bridge`) as
`COMPONENT_<KIND>_*` Make variables. The stock simulation/FPGA Makefiles
naturally need the plug-ins they instantiate; non-stock designs may use their
own top/harness. Unknown configuration settings are preserved as
`COMPONENT_SETTING_*` Make variables instead of rejected.

The only contract imposed by the stock integration is at the point a chosen
module is plugged in:

| Selection | Stock integration module | Purpose |
|---|---|---|
| `core` | `axcore` | instruction/data aXbus masters, interrupt inputs, and the small commit signal set used for `fence.i` |
| `memory` | `axmem` | independent instruction/data aXbus RAM ports plus optional SDRAM pins |
| `interconnect` | `axbus_mux` | one aXbus master decode and response selection |
| `cache` | `axcache` | optional core-port cache or transparent forwarding policy |
| `rom` | `axrom` | dual-port boot ROM |
| `finisher` | `test_finisher` | simulation termination endpoint |
| `uart` | `uart` | UART0 aXbus device and byte-level RX/TX sideband |
| `clint` | `clint` | CLINT aXbus device plus software/timer IRQs |
| `spi` | `axspi` | SPI0 aXbus device and four SPI pins |
| `soc` | `soc_top` | complete-SoC simulation/board shell |
| `software` | its own Make target and produced image(s) | payload built independently, then supplied to the selected hardware profile |
| `harness` | selected simulation top + C++ testbench | simulation environment kept separate from board RTL |
| `scheduler` | `scheduler_select` | choose a runnable aXos task; context switching and syscalls stay in the kernel |
| `vm` | `vm_*` | bootstrap mappings and the user part of an aXos task address space |
| `allocator` | `page_*` | physical-page allocation policy |
| `shell` | `shell_run` | aXos console command loop |
| `filesystem` | `fs_*` | mounted filesystem policy |
| `block` | `sd_*` | block-device transport |

The real SystemVerilog instantiation is the authoritative port list; no second
hand-maintained IDL can drift from it. A user who wants a different boundary
can select a custom `soc` component, use a custom test harness, or build their
own board top. The component system deliberately does not fence them in.

Software has no source-level ABI requirement. A manifest names its own Make
directory/target and whether it produces a RAM hex image or an SD-boot payload.
`make software CONFIG=configs/sim-hello.json` runs the supplied bare-metal
image; `make software CONFIG=configs/sim-axos.json` builds aXos and boots it
through the selected SDRAM/SD profile. An external kernel may use the same
small manifest fields while keeping all of its sources and build rules outside
atomiX.

For the supplied aXos kernel, `KERNEL_CONFIG` selects the scheduler, VM,
allocator, shell, filesystem, and block components independently of the system
profile. The default is
`configs/kernel-default.json`; `configs/kernel-cooperative.json` is a working
alternative policy that retains a task until it blocks or exits. An external
component may include `sw/kernel/include/scheduler.h` or `vm.h`, build beside
its manifest, and be selected through the same `{ "manifest": ... }` form.
These headers intentionally expose no trap or syscall internals.

The repository-wide selection policy and its intentional private boundaries
are in [docs/component-map.md](../docs/component-map.md).
The shortest profile-to-verification path is in
[docs/build.md](../docs/build.md).

## External component example

An implementation may live anywhere. Create a manifest next to its sources:

```json
{
  "schema": 1,
  "id": "mycore.singlecycle",
  "kind": "core",
  "title": "My single-cycle experiment",
  "sources": ["mycore.sv"],
  "stock_soc_module": "axcore",
  "capabilities": ["rv32i", "single-cycle"]
}
```

Then select it without copying anything into this repository:

```json
"core": {"manifest": "../my-components/mycore/component.json"}
```

inside a configuration's `components` object. For an external manifest,
`sources` are relative to that manifest's folder. Built-in manifests use paths
relative to the repository root.

Run `python3 tools/configure.py resolve --config path/to/config.json` first;
the compiler and the selected test suite are the next compatibility gates.
Claiming conformance to the reference CPU's ISA/formal/cosim status remains a
separate, evidence-based decision by the component author.
