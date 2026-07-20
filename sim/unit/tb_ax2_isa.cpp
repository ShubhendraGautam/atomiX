// tb_ax2_isa: run the official riscv-tests ISA suites against the ax2 core.
//
// This is the ISA-conformance leg of the ax2 suite.  ax2 has no RVFI surface,
// so it cannot be checked in lock-step against the golden ISS the way the
// reference core is; what it can do is execute the same official rv32ui/rv32um/
// rv32mi binaries the reference core passes and report the same verdict.  Those
// binaries are self-checking: each one runs a battery of cases and signals the
// result by storing to the `tohost` location -- 1 means pass, anything else
// encodes the number of the case that failed.
//
// The memory model is the flat one from tb_axcore (RAM, a 16550 TX subset, and
// the sifive_test finisher) plus the tohost watch, which is the only thing the
// ISA environment needs that the directed tests do not.
//
// usage: tb_ax2_isa PROG.bin --tohost ADDR [--ws N] [--max CYCLES]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include "Vaxcore.h"
#include "verilated.h"

static constexpr uint32_t RAM_BASE = 0x80000000, RAM_SIZE = 4u << 20;
static constexpr uint32_t UART_BASE = 0x10000000, TEST_BASE = 0x00100000;

static std::vector<uint8_t> ram(RAM_SIZE, 0);
static bool exit_req = false;
static int exit_code = 0;
static int waitstates = 0;
static uint32_t tohost_addr = 0;
static uint32_t tohost_val = 0;

static bool mapped(uint32_t a) {
  return (a >= RAM_BASE && a < RAM_BASE + RAM_SIZE) ||
         (a >= UART_BASE && a < UART_BASE + 0x1000) ||
         (a >= TEST_BASE && a < TEST_BASE + 0x1000);
}

static uint32_t bus_read(uint32_t a) {
  if (a >= RAM_BASE && a < RAM_BASE + RAM_SIZE) {
    const uint32_t off = a - RAM_BASE;
    return ram[off] | ram[off + 1] << 8 | ram[off + 2] << 16 |
           (uint32_t)ram[off + 3] << 24;
  }
  if (a == UART_BASE + 4) return 0x60u << 8;   // LSR: THR empty
  return 0;
}

static void bus_write(uint32_t a, uint32_t d, uint8_t strb) {
  if (a >= RAM_BASE && a < RAM_BASE + RAM_SIZE) {
    for (int i = 0; i < 4; i++)
      if (strb & (1 << i)) ram[a - RAM_BASE + i] = (uint8_t)(d >> (8 * i));
    // The ISA environment reports its verdict here.  Bit 0 set means "done";
    // the remaining bits carry the failing case number, so 1 is a clean pass.
    if (tohost_addr && a == tohost_addr && (strb & 0xf) && d != 0) {
      tohost_val = d;
      exit_req = true;
      exit_code = (d == 1) ? 0 : (int)(d >> 1);
      if (d != 1 && exit_code == 0) exit_code = 1;
    }
  } else if (a == UART_BASE && (strb & 1)) {
    fputc(d & 0xFF, stdout);
    fflush(stdout);
  } else if (a >= TEST_BASE && a < TEST_BASE + 0x1000) {
    const uint32_t status = d & 0xFFFF;
    if (status == 0x5555 || status == 0x7777) { exit_req = true; exit_code = 0; }
    else if (status == 0x3333) {
      exit_req = true;
      exit_code = (int)(d >> 16);
      if (!exit_code) exit_code = 1;
    }
  }
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const char *bin = nullptr;
  uint64_t max_cycles = 4000000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--ws") && i + 1 < argc) waitstates = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--max") && i + 1 < argc)
      max_cycles = strtoull(argv[++i], nullptr, 0);
    else if (!strcmp(argv[i], "--tohost") && i + 1 < argc)
      tohost_addr = (uint32_t)strtoul(argv[++i], nullptr, 0);
    else bin = argv[i];
  }
  if (!bin) {
    fprintf(stderr, "usage: tb_ax2_isa PROG.bin --tohost ADDR [--ws N]\n");
    return 1;
  }

  FILE *f = fopen(bin, "rb");
  if (!f) { perror(bin); return 1; }
  const size_t n = fread(ram.data(), 1, ram.size(), f);
  fclose(f);
  if (n == 0) { fprintf(stderr, "empty program\n"); return 1; }

  Vaxcore *top = new Vaxcore;
  int icnt = 0, dcnt = 0;

  top->rst = 1; top->clk = 0;
  top->irq_software = 0; top->irq_timer = 0; top->irq_external = 0;
  top->eval();
  top->clk = 1; top->eval();
  top->clk = 0; top->rst = 0; top->eval();

  uint64_t cyc = 0;
  for (; cyc < max_cycles && !exit_req; cyc++) {
    top->clk = 0; top->eval();
    for (int it = 0; it < 3; it++) {     // settle the valid/ready comb loop
      top->ibus_ready = top->ibus_valid && icnt >= waitstates;
      top->ibus_rdata = bus_read(top->ibus_addr);
      top->ibus_err   = top->ibus_valid && !mapped(top->ibus_addr);
      top->dbus_ready = top->dbus_valid && dcnt >= waitstates;
      top->dbus_rdata = bus_read(top->dbus_addr);
      top->dbus_err   = top->dbus_valid && !mapped(top->dbus_addr);
      top->eval();
    }

    const bool icomp = top->ibus_valid && top->ibus_ready;
    const bool dcomp = top->dbus_valid && top->dbus_ready;
    const bool ivalid = top->ibus_valid, dvalid = top->dbus_valid;
    const uint32_t daddr = top->dbus_addr, dw = top->dbus_wdata;
    const uint8_t dstrb = top->dbus_wstrb;

    top->clk = 1; top->eval();

    if (dcomp && dstrb && !top->dbus_err) bus_write(daddr, dw, dstrb);
    icnt = icomp ? 0 : (ivalid ? icnt + 1 : 0);
    dcnt = dcomp ? 0 : (dvalid ? dcnt + 1 : 0);
  }

  delete top;
  if (!exit_req) {
    fprintf(stderr, "[tb_ax2_isa] TIMEOUT after %llu cycles\n",
            (unsigned long long)cyc);
    return 124;
  }
  if (exit_code)
    fprintf(stderr, "[tb_ax2_isa] FAIL tohost=0x%x (case %d, cycles=%llu)\n",
            tohost_val, exit_code, (unsigned long long)cyc);
  return exit_code;
}
