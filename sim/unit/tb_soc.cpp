// SoC smoke test: boot from ROM, send one byte through the 16550 subset, and
// finish through the QEMU-compatible sifive_test endpoint.
#include <cstdio>

#include "Vsoc_top.h"
#include "verilated.h"

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vsoc_top* top = new Vsoc_top;
  top->rst = 1;
  top->clk = 0;
  top->irq_external = 0;
  top->uart_tx_ready = 1;
  top->sdram_dq_i = 0;
  top->eval();
  top->clk = 1;
  top->eval();
  top->clk = 0;
  top->rst = 0;
  top->eval();

  unsigned tx_count = 0;
  unsigned char tx_byte = 0;
  for (unsigned cycle = 0; cycle < 100 && !top->finished; ++cycle) {
    top->clk = 0;
    top->eval();
    top->clk = 1;
    top->eval();
    if (top->uart_tx_valid) {
      ++tx_count;
      tx_byte = top->uart_tx_data;
    }
  }

  const bool ok = top->finished && top->exit_code == 0 &&
                  tx_count == 1 && tx_byte == 'A';
  if (!ok) {
    std::fprintf(stderr, "tb_soc: FAIL finished=%d exit=%u tx_count=%u tx=%02x\n",
                 top->finished, top->exit_code, tx_count, tx_byte);
    delete top;
    return 1;
  }
  delete top;
  std::puts("tb_soc: PASS");
  return 0;
}
