// Generic full-SoC runner used by bare-metal integration tests. The loaded
// program is expected to use the standard UART and sifive_test interfaces.
#include <cstdio>
#include <string>

#include "Vsoc_top.h"
#include "verilated.h"

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vsoc_top* top = new Vsoc_top;
  top->rst = 1;
  top->clk = 0;
  top->irq_external = 0;
  top->eval();
  top->clk = 1;
  top->eval();
  top->clk = 0;
  top->rst = 0;
  top->eval();

  std::string uart;
  unsigned cycles = 0;
  for (; cycles < 100000 && !top->finished; ++cycles) {
    top->clk = 0;
    top->eval();
    top->clk = 1;
    top->eval();
    if (top->uart_tx_valid) uart.push_back(char(top->uart_tx_data));
  }

  std::fwrite(uart.data(), 1, uart.size(), stdout);
  const bool ok = top->finished && top->exit_code == 0;
  if (!ok) {
    std::fprintf(stderr, "[soc] FAIL finished=%d exit=%u cycles=%u\n",
                 top->finished, top->exit_code, cycles);
    delete top;
    return 1;
  }
  std::fprintf(stderr, "[soc] exit 0 (cycles=%u)\n", cycles);
  delete top;
  return 0;
}
