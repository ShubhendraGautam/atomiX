# sim/soc/ — complete-SoC simulations

This runner instantiates `rtl/soc/soc_top.sv`, initializes its RAM from a
word-per-line `$readmemh` image, and captures UART output until the standard
`sifive_test` finisher exits. The UART includes a one-byte RX holding register:
pass a byte script with `UART_INPUT_FILE` to drive a software console.

It is driven by the bare-metal build:

```bash
make -C sw/baremetal run-rtl
```

For a direct invocation, provide a RAM image and an entry point:

```bash
make -C sim/soc run RAM_INIT_FILE=/absolute/path/program.hex RESET_PC=0x80000000
```

For a scripted aXos shell session:

```bash
make -C sw/kernel run-rtl UART_INPUT_FILE="$PWD/sw/kernel/shell_input.txt"
```
