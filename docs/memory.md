# Phase 6 memory architecture

Status: the first, board-independent real-memory milestone is implemented and
regressed. It provides a delayed 32 MiB backing store and split I/D caches in
RTL simulation. The polling SPI controller, SDHC command model, and a
deterministic one-sector block-read regression are also implemented. It is
deliberately **not** an SDRAM PHY/controller, a writable filesystem, or an SD
bootloader; those remain required for the Phase 6 exit criterion.

## Boundary and configuration

`soc_top` keeps aXcore's two physical aXbus ports unchanged. With caches
enabled, each port passes through an `axcache` before the existing `axbus_mux`:

```text
aXcore ibus ── I$ ── aXbus mux ── RAM / ROM / MMIO
aXcore dbus ── D$ ── aXbus mux ── RAM / ROM / MMIO
                                 └─ axdram_model (simulation configuration)
```

The cache boundary only claims the RAM range `0x8000_0000 .. RAM_BYTES-1`.
ROM, CLINT, UART, test finisher, and decode errors bypass it exactly once; no
device register is cached. The default SoC remains the Phase 5, 128 KiB
dual-port BRAM configuration. Select the Phase 6 simulation configuration
explicitly:

```bash
make -C sw/kernel run-rtl \
  UART_INPUT_FILE="$PWD/sw/kernel/shell_input.txt" \
  RAM_BYTES=33554432 EXTERNAL_MEMORY=1 CACHES=1
```

`MAX_CYCLES` defaults to 100000 in the SoC runner. The cache/Sv32 fork demo is
intentionally more demanding, so the regression sets it to 500000; it may be
overridden for interactive experiments.

## Delayed backing store

`rtl/soc/axdram_model.sv` implements the future controller-facing shape today:
one independently stalled aXbus port for instruction traffic and one for data
traffic. Each request is latched, responds after a fixed three-cycle delay,
honors byte writes, and reports malformed/out-of-range requests at completion.
The backing array is `$readmemh` initialized, so the normal RAM image loader
continues to work.

This is a timing and protocol model, not a claim that an FPGA has 32 MiB of
BRAM. A board-specific replacement must add SDRAM initialization, refresh,
command timing, pin-level PHY work, and arbitration for the physical SDRAM
channel while preserving the aXbus completion and fault contract.

## Cache contract

`rtl/soc/axcache.sv` is a small direct-mapped cache: 16 lines of four 32-bit
words (16-byte lines). It is intentionally small so the control path is easy
to audit before choosing board RAM resources.

- Reads allocate and refill one complete line.
- Writes are write-through, never allocate, and invalidate their local line.
- A fetch-side Sv32 A-bit write invalidates the D-cache on the following
  cycle, preventing a stale PTE data view.
- `FENCE.I` retires serially in aXcore. `soc_top` recognizes that committed
  instruction from the existing trace, registers a one-cycle I-cache flush,
  and the already-serialized refetch cannot complete from a stale line.
- Plain `FENCE` has no extra hardware action: writes are ordered and
  write-through on this single-hart implementation.

The caches see physical addresses after Sv32 translation. There is no DMA or
second hart yet; when either arrives, this deliberately simple invalidation
scheme must be replaced or extended by a defined coherency policy.

## Regression commands

No additional host dependency is needed beyond the documented RISC-V GCC and
Verilator setup.

```bash
make -C sim/unit run-axdram-model  # delayed-memory timing/data/error contract
make -C sim/unit run-axcache       # fills, hits, write-through, flush, bypass
make -C sw/baremetal check-fencei QEMU="$HOME/.local/bin/qemu-system-riscv32"
make -C sw/kernel check-memory     # cached 32 MiB RTL shell + fork/wait
```

The `check-fencei` image fetches an instruction, patches it through the data
port, executes `fence.i`, and verifies the second call observes the patched
instruction on ISS, QEMU, and cached delayed RTL.

## SPI SD-card foundation

`rtl/soc/axspi.sv` is a polling SPI mode-0 controller at `0x1001_0000`.
`DATA` (`+0`) holds the transmit/received byte, `CTRL` (`+4`) controls
`GO` bit 0 and `CS_N` bit 1, `STATUS` (`+8`) reports `BUSY` bit 0 and
`RX_VALID` bit 1, and `CLKDIV` (`+12`) sets the half-cycle divider. The three
pin-level signals (`spi_sclk`, `spi_mosi`, `spi_cs_n`) and `spi_miso` are on
`soc_top` for the later board top.

The SoC runner has an SPI-mode SDHC simulation card; pass `SD_IMAGE=path` to
load a binary sector image. It implements CMD0, CMD8, CMD55/ACMD41, CMD16,
CMD17, and CMD58, with 512-byte SDHC block addressing. It is intentionally
read-only and exists only in the C++ simulation harness. The hardware
controller itself is synthesizable and can drive a physical card.

```bash
make -C sim/unit run-axspi        # controller register/waveform contract
make -C sw/baremetal check-spi    # SoC decode plus idle-MISO smoke image
make -C sw/baremetal check-sd     # SDHC init + CMD17 sector read on RTL
```

The next storage milestone is a disk-image format and kernel block driver;
the AXFS v1 read-only format and kernel block driver now provide that path:

```bash
make -C sw/kernel check-storage
```

This builds a deterministic SD image containing `motd` and `readme`, mounts it
through the kernel SPI driver, and runs the normal shell plus fork/wait script
on cached delayed RTL. The regression requires the SD-specific `readme`
transcript, so it cannot pass through the RAM-disk fallback. It preserves the
Phase 5 RAM-disk fallback for ISS and
QEMU; replacing that fallback and loading the kernel itself from SD remain the
boot-path work.
