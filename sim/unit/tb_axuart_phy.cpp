#include <cstdio>
#include <cstdint>

#include "Vaxuart_phy_loopback_top.h"
#include "verilated.h"

namespace {
void tick(Vaxuart_phy_loopback_top* top) {
  top->clk = 0;
  top->eval();
  top->clk = 1;
  top->eval();
}

bool send_and_receive(Vaxuart_phy_loopback_top* top, uint8_t byte) {
  for (unsigned i = 0; i < 20 && !top->tx_ready; ++i) tick(top);
  if (!top->tx_ready) return false;
  top->tx_data = byte;
  top->tx_valid = 1;
  tick(top);
  top->tx_valid = 0;
  for (unsigned i = 0; i < 300; ++i) {
    tick(top);
    if (top->rx_valid) {
      const bool ok = top->rx_data == byte;
      tick(top);  // rx_ready consumes the byte
      return ok;
    }
  }
  return false;
}
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  auto* top = new Vaxuart_phy_loopback_top;
  top->clk = 0;
  top->rst = 1;
  top->tx_valid = 0;
  top->tx_data = 0;
  top->rx_ready = 1;
  tick(top);
  top->rst = 0;
  const bool ok = send_and_receive(top, 0xa5) && send_and_receive(top, 0x3c);
  delete top;
  if (!ok) {
    std::fputs("tb_axuart_phy: FAIL\n", stderr);
    return 1;
  }
  std::puts("tb_axuart_phy: PASS");
  return 0;
}
