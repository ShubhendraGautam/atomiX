// Integration test for ROM -> CLINT -> aXcore timer interrupt -> finisher.
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

  unsigned cycles = 0;
  for (; cycles < 1000 && !top->finished; ++cycles) {
    top->clk = 0;
    top->eval();
    top->clk = 1;
    top->eval();
  }
  const bool ok = top->finished && top->exit_code == 0;
  if (!ok) {
    std::fprintf(stderr, "tb_soc_timer: FAIL finished=%d exit=%u cycles=%u\n",
                 top->finished, top->exit_code, cycles);
    delete top;
    return 1;
  }
  delete top;
  std::puts("tb_soc_timer: PASS");
  return 0;
}
