# sw/bootrom/ — SD boot ROM

This is a small M-mode loader linked at `0x0000_1000`. It uses the polling
SPI controller to initialize an SDHC card, reads an `AXBT` header and raw
kernel sectors, copies them to `0x8000_0000`, then jumps to the normal aXos
entry point. Scratch BSS and the boot stack live in RAM at `0x8001_0000` while
the kernel is loaded.

The reproducible end-to-end check is run from the kernel directory:

```bash
make -C sw/kernel check-sdboot
```

It builds this ROM, a storage-enabled kernel, and a combined SD image before
starting the RTL SoC at the ROM reset address.
