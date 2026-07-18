# Component map and selection policy

atomiX is a DIY platform: every meaningful implementation boundary is a
component candidate. "Meaningful" is deliberate. A boundary qualifies when a
user might reasonably replace it without also replacing its caller, it owns a
policy or backend, and its source can be selected independently. Small helper
functions, pipeline sub-stages, and test utilities remain private to the
implementation that owns them; making every function a plug-in would make the
system harder to change without giving users a useful choice.

The resolver accepts external manifests for every listed kind. A stock profile
selects known-compatible implementations, but it does not assert that an
alternative has the reference ISA, timing, filesystem, or verification status.

| Layer | Selected kinds | Stock implementation choices |
|---|---|---|
| CPU | `core` | five-stage `core.pipeline5`; executable finisher-only smoke core |
| SoC fabric | `interconnect`, `cache`, `memory`, `rom`, `finisher`, `soc` | aXbus mux, direct-mapped or transparent cache, BRAM/delayed/SDRAM memory, boot ROM, SiFive-test finisher |
| Peripherals | `uart`, `clint`, `spi` | QEMU-virt-aligned UART/CLINT and polling SPI |
| Build environments | `board`, `harness` | ULX3S board top; normal and physical-SDRAM Verilator harnesses |
| Payload | `software` | bare-metal hello or SD-boot aXos; an external software manifest may build any kernel/monitor |
| aXos policy/services | `scheduler`, `vm`, `allocator`, `shell`, `filesystem`, `block` | round-robin or cooperative scheduler; Sv32 VM; free-list allocator; AX shell/AXFS/SPI-SD services |

The two executable non-reference selections are intentional examples:
`core.finisher-smoke` proves CPU source replacement, and
`cache.passthrough` proves that a profile can replace the cache while retaining
the SoC interface. `scheduler.cooperative` proves a kernel-policy replacement.
They are composition evidence only; they do not inherit every claim made for
the reference implementation.

## Current boundaries and future work

The current manifests cover everything selected by the stock simulation, FPGA,
software, and kernel build pipelines. Built-in component RTL, board assets,
harnesses, and aXos services are
  co-located with their manifests. The `memory.*` profiles intentionally share
  the private `components/memory/reference/` source family because BRAM,
  delayed-memory, and SDRAM are compatible configurations of one `axmem`
  implementation. `rtl/`, `sim/soc/`, and `sw/kernel/` retain architecture,
  runner, and kernel-orchestration material rather than duplicating selectable
  implementations.

The following items are not missing components; they are future systems that
will introduce their own coherent boundaries when they exist:

- Host-link transports and accelerator roles will be `host_link` and `role`
  components, with a virtual-pipe simulation implementation first.
- Userland, bootloader, and alternate operating systems are `software`
  components today. A project that wants a distinct boot chain owns it through
  its software manifest instead of being forced into aXos internals.
- Test generators, formal jobs, and ISS/cosimulation are verification tools,
  not runtime implementations. They validate every selected component rather
  than being silently substituted for one another. Their profiles and commands
  stay explicit evidence gates.
- ECP5 place-and-route and physical ULX3S proof are the final hardware gate;
  they do not block simulation-first component work.

Use `make component-list` to inspect the catalog, `make component-show
COMPONENT=cache.passthrough` for one manifest, and `make component-test` for
the supplied composition matrix. The authoritative source-level contracts are
the module instantiations and the kernel headers; see
[components/README.md](../components/README.md) for external manifest syntax.
