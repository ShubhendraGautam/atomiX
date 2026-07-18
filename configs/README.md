# atomiX configuration profiles

Each JSON file selects a set of components for one reproducible build. The
resolver accepts built-in component IDs or an external manifest path:

```json
{
  "schema": 1,
  "name": "my-machine",
  "components": {
    "core": {"manifest": "../my-core/component.json"},
    "soc": "soc.reference",
    "memory": "memory.delayed",
    "uart": "uart.mmio16550",
    "clint": "clint.qemu-virt",
    "spi": "spi.polling-mode0",
    "board": "board.sim",
    "software": "software.baremetal-hello"
  },
  "settings": {
    "ram_bytes": 33554432,
    "caches": true,
    "reset_pc": "0x80000000"
  }
}
```

The stock simulation/FPGA Makefiles need the components they instantiate; the
resolver itself also accepts partial and non-stock compositions for a custom
top or harness. The three stock peripheral selections are currently needed by
`soc.reference`. Unknown `settings` remain available as
`COMPONENT_SETTING_*` Make variables. They are deliberately not rejected, so a
custom component may define its own knobs without changing the common resolver.

| Profile | Purpose |
|---|---|
| `sim-bram.json` | reference CPU and SoC with 128 KiB BRAM |
| `sim-delayed.json` | 32 MiB delayed backing store plus I/D caches |
| `sim-sdram.json` | x16 SDRAM controller against the behavioral SDRAM model |
| `sim-finisher.json` | alternate minimal CPU composition smoke test; not RISC-V |
| `sim-hello.json` | reference BRAM machine plus selectable bare-metal payload |
| `sim-axos.json` | reference SDRAM machine plus selectable aXos SD-boot payload |
| `ulx3s-85f.json` | ULX3S/ECP5 board implementation and constraints |

Validate before building:

```bash
make config-check CONFIG=configs/sim-sdram.json
make config-check-all
make software CONFIG=configs/sim-hello.json
make software CONFIG=configs/sim-axos.json
```
