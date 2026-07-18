// Public runner source for harness.verilator-soc. The executable body is
// shared with the physical-SDRAM harness so UART, SPI-SD, and exit handling
// remain identical across the two simulation environments.
#include "../common/tb_soc_main.cpp"
