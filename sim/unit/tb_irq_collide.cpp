// Forces a timer interrupt onto the exact commit cycle of a marker
// instruction (a serialized CSR write or an mret). Pass 1 runs interrupt-free
// and records the cycle the marker commits on (trace_valid && trace_insn);
// pass 2 replays the identical program with irq_timer asserted from precisely
// that cycle, guaranteeing the collision. The program self-checks the
// architectural state and reports through the test finisher.
//
// Usage: tb_irq_collide <image.bin> <marker-insn-hex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Vaxcore.h"
#include "verilated.h"

static constexpr uint32_t RAM_BASE = 0x80000000;
static constexpr uint32_t RAM_SIZE = 16384;
static constexpr uint32_t TEST_BASE = 0x00100000;

static uint32_t get_word(const std::vector<uint8_t>& ram, uint32_t addr) {
  const uint32_t off = addr - RAM_BASE;
  return ram[off] | (uint32_t(ram[off + 1]) << 8) |
         (uint32_t(ram[off + 2]) << 16) | (uint32_t(ram[off + 3]) << 24);
}

struct Result {
  bool finished = false;
  int code = -1;          // finisher exit code; 0 = pass
  long marker_cycle = -1; // cycle the marker instruction committed on
};

// irq_at < 0: never assert irq_timer (probe pass).
static Result run(const std::vector<uint8_t>& ram, uint32_t marker,
                  long irq_at) {
  Vaxcore* top = new Vaxcore;
  Result r;

  top->rst = 1;
  top->clk = 0;
  top->ibus_ready = 0;
  top->dbus_ready = 0;
  top->ibus_rdata = 0;
  top->dbus_rdata = 0;
  top->ibus_err = 0;
  top->dbus_err = 0;
  top->irq_software = 0;
  top->irq_timer = 0;
  top->irq_external = 0;
  top->eval();
  top->clk = 1;
  top->eval();
  top->clk = 0;
  top->rst = 0;
  top->eval();

  for (long cycle = 0; cycle < 400 && !r.finished; ++cycle) {
    top->irq_timer = (irq_at >= 0 && cycle >= irq_at) ? 1 : 0;
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

    if (top->trace_valid && top->trace_insn == marker && r.marker_cycle < 0)
      r.marker_cycle = cycle;
    const bool dcomp = top->dbus_valid && top->dbus_ready && !top->dbus_err;
    const uint32_t daddr = top->dbus_addr;
    const uint32_t dwdata = top->dbus_wdata;
    const uint8_t dwstrb = top->dbus_wstrb;
    top->clk = 1;
    top->eval();
    if (dcomp && dwstrb && daddr == TEST_BASE) {
      r.finished = true;
      const uint32_t status = dwdata & 0xffff;
      r.code = (status == 0x5555) ? 0 : (int)(dwdata >> 16);
      if (status == 0x3333 && r.code == 0) r.code = -1;
    }
  }
  delete top;
  return r;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  if (argc != 3) {
    std::fprintf(stderr, "usage: tb_irq_collide <image.bin> <marker-hex>\n");
    return 2;
  }
  const uint32_t marker = (uint32_t)strtoul(argv[2], nullptr, 16);

  std::vector<uint8_t> ram(RAM_SIZE, 0);
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) { std::perror(argv[1]); return 2; }
  const size_t n = std::fread(ram.data(), 1, ram.size(), f);
  std::fclose(f);
  if (n == 0 || n == ram.size()) {
    std::fprintf(stderr, "tb_irq_collide: bad image size %zu\n", n);
    return 2;
  }

  const Result probe = run(ram, marker, -1);
  if (probe.marker_cycle < 0) {
    std::fprintf(stderr,
                 "tb_irq_collide: %s: marker %08x never committed\n",
                 argv[1], marker);
    return 1;
  }

  const Result hit = run(ram, marker, probe.marker_cycle);
  const bool collided = hit.marker_cycle == probe.marker_cycle;
  if (!hit.finished || hit.code != 0 || !collided) {
    std::fprintf(stderr,
                 "tb_irq_collide: %s FAIL finished=%d code=%d "
                 "marker_cycle=%ld (probe %ld)\n",
                 argv[1], hit.finished, hit.code, hit.marker_cycle,
                 probe.marker_cycle);
    return 1;
  }
  std::printf("tb_irq_collide: %s PASS (collision at cycle %ld)\n", argv[1],
              probe.marker_cycle);
  return 0;
}
