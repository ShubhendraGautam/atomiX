// Directed core test for precise machine-timer interrupts.  The timer is
// asserted from reset; the program enables MTIE and mstatus.MIE, and its trap
// handler writes the standard finisher.  This checks both the architectural
// trap state and that the interrupted NOP retired before control transferred.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Vaxcore.h"
#include "verilated.h"

static constexpr uint32_t RAM_BASE = 0x80000000;
static constexpr uint32_t RAM_SIZE = 4096;
static constexpr uint32_t TEST_BASE = 0x00100000;

static uint32_t get_word(const std::vector<uint8_t>& ram, uint32_t addr) {
  const uint32_t off = addr - RAM_BASE;
  return ram[off] | (uint32_t(ram[off + 1]) << 8) |
         (uint32_t(ram[off + 2]) << 16) | (uint32_t(ram[off + 3]) << 24);
}

static void put_word(std::vector<uint8_t>& ram, uint32_t addr, uint32_t value) {
  const uint32_t off = addr - RAM_BASE;
  for (unsigned i = 0; i < 4; ++i) ram[off + i] = value >> (8 * i);
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  std::vector<uint8_t> ram(RAM_SIZE, 0);

  // _start: install handler, enable MTIE then MIE, and execute a NOP.
  put_word(ram, RAM_BASE + 0x00, 0x800000b7);  // lui   x1, 0x80000
  put_word(ram, RAM_BASE + 0x04, 0x04008093);  // addi  x1, x1, 64
  put_word(ram, RAM_BASE + 0x08, 0x30509073);  // csrw  mtvec, x1
  put_word(ram, RAM_BASE + 0x0c, 0x08000093);  // addi  x1, x0, 128
  put_word(ram, RAM_BASE + 0x10, 0x30409073);  // csrw  mie, x1
  put_word(ram, RAM_BASE + 0x14, 0x00800093);  // addi  x1, x0, 8
  put_word(ram, RAM_BASE + 0x18, 0x3000a073);  // csrrs x0, mstatus, x1
  put_word(ram, RAM_BASE + 0x1c, 0x00000013);  // nop (retires before IRQ)
  put_word(ram, RAM_BASE + 0x20, 0x0000006f);  // jal   x0, 0

  // trap_handler: store 0x5555 to the test finisher.
  put_word(ram, RAM_BASE + 0x40, 0x00100137);  // lui   x2, 0x00100
  put_word(ram, RAM_BASE + 0x44, 0x000051b7);  // lui   x3, 0x5
  put_word(ram, RAM_BASE + 0x48, 0x55518193);  // addi  x3, x3, 0x555
  put_word(ram, RAM_BASE + 0x4c, 0x00312023);  // sw    x3, 0(x2)

  Vaxcore* top = new Vaxcore;
  top->rst = 1;
  top->clk = 0;
  top->ibus_ready = 0;
  top->dbus_ready = 0;
  top->ibus_rdata = 0;
  top->dbus_rdata = 0;
  top->ibus_err = 0;
  top->dbus_err = 0;
  top->irq_software = 0;
  top->irq_timer = 1;
  top->irq_external = 0;
  top->eval();
  top->clk = 1;
  top->eval();
  top->clk = 0;
  top->rst = 0;
  top->eval();

  bool finished = false;
  for (unsigned cycle = 0; cycle < 200 && !finished; ++cycle) {
    top->clk = 0;
    top->eval();
    for (int settle = 0; settle < 3; ++settle) {
      const bool i_ok = top->ibus_addr >= RAM_BASE &&
                        top->ibus_addr - RAM_BASE <= RAM_SIZE - 4;
      const bool d_ram = top->dbus_addr >= RAM_BASE &&
                         top->dbus_addr - RAM_BASE <= RAM_SIZE - 4;
      const bool d_test = top->dbus_addr >= TEST_BASE &&
                          top->dbus_addr - TEST_BASE < 0x1000;
      top->ibus_ready = top->ibus_valid;
      top->ibus_rdata = i_ok ? get_word(ram, top->ibus_addr) : 0;
      top->ibus_err = top->ibus_valid && !i_ok;
      top->dbus_ready = top->dbus_valid;
      top->dbus_rdata = d_ram ? get_word(ram, top->dbus_addr) : 0;
      top->dbus_err = top->dbus_valid && !(d_ram || d_test);
      top->eval();
    }

    const bool dcomp = top->dbus_valid && top->dbus_ready && !top->dbus_err;
    const uint32_t daddr = top->dbus_addr;
    const uint32_t dwdata = top->dbus_wdata;
    const uint8_t dwstrb = top->dbus_wstrb;
    top->clk = 1;
    top->eval();
    if (dcomp && dwstrb && daddr == TEST_BASE && (dwdata & 0xffff) == 0x5555)
      finished = true;
  }

  const bool state_ok = top->trace_mcause == 0x80000007 &&
                        top->trace_mepc == RAM_BASE + 0x20 &&
                        top->trace_mip == 0x00000080 &&
                        !(top->trace_mstatus & 0x8) &&
                        (top->trace_mstatus & 0x80);
  if (!finished || !state_ok) {
    std::fprintf(stderr,
                 "tb_interrupt: FAIL finished=%d mcause=%08x mepc=%08x mip=%08x mstatus=%08x\n",
                 finished, top->trace_mcause, top->trace_mepc, top->trace_mip,
                 top->trace_mstatus);
    delete top;
    return 1;
  }
  delete top;
  std::puts("tb_interrupt: PASS");
  return 0;
}
