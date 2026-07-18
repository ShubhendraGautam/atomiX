// Protocol and data-integrity test for the Phase 6 external-memory model.
// Every request holds valid until ready, exactly as the aXbus master must.
#include <cstdint>
#include <cstdio>

#include "Vaxdram_model.h"
#include "verilated.h"

static constexpr uint32_t kBase = 0x80000000u;
static constexpr int kLatency = 3;

struct response {
  uint32_t data;
  bool error;
  int wait_cycles;
};

static int failures = 0;

static void check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    failures++;
  }
}

static void tick(Vaxdram_model* top) {
  top->clk = 0;
  top->eval();
  top->clk = 1;
  top->eval();
  top->clk = 0;
  top->eval();
}

static response transact_i(Vaxdram_model* top, uint32_t address,
                           uint32_t data, uint8_t strobe) {
  top->i_addr = address;
  top->i_wdata = data;
  top->i_wstrb = strobe;
  top->i_valid = 1;
  top->eval();
  check(!top->i_ready, "I port must not complete combinationally");

  int cycles = 0;
  while (!top->i_ready) {
    tick(top);
    check(++cycles <= kLatency, "I port response arrived too late");
  }
  const response result = {top->i_rdata, bool(top->i_err), cycles};
  tick(top);  // Complete the ready/valid transaction at this rising edge.
  top->i_valid = 0;
  top->eval();
  return result;
}

static response transact_d(Vaxdram_model* top, uint32_t address,
                           uint32_t data, uint8_t strobe) {
  top->d_addr = address;
  top->d_wdata = data;
  top->d_wstrb = strobe;
  top->d_valid = 1;
  top->eval();
  check(!top->d_ready, "D port must not complete combinationally");

  int cycles = 0;
  while (!top->d_ready) {
    tick(top);
    check(++cycles <= kLatency, "D port response arrived too late");
  }
  const response result = {top->d_rdata, bool(top->d_err), cycles};
  tick(top);
  top->d_valid = 0;
  top->eval();
  return result;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vaxdram_model top;
  top.clk = 0;
  top.rst = 1;
  top.i_valid = top.d_valid = 0;
  top.i_addr = top.d_addr = 0;
  top.i_wdata = top.d_wdata = 0;
  top.i_wstrb = top.d_wstrb = 0;
  tick(&top);
  top.rst = 0;
  top.eval();

  response r = transact_i(&top, kBase + 4, 0x11223344u, 0xf);
  check(!r.error && r.wait_cycles == kLatency, "I full-word write latency");

  r = transact_d(&top, kBase + 4, 0x0000aa00u, 0x2);
  check(!r.error && r.wait_cycles == kLatency, "D byte write latency");
  r = transact_i(&top, kBase + 4, 0, 0);
  check(!r.error && r.data == 0x1122aa44u, "byte-lane writes preserve other bytes");

  // Both ports are independent, so unrelated transfers can make progress
  // together and their ready responses must arrive in the same fixed delay.
  top.i_addr = kBase + 8;
  top.i_wdata = 0x55667788u;
  top.i_wstrb = 0xf;
  top.i_valid = 1;
  top.d_addr = kBase + 12;
  top.d_wdata = 0xaabbccddu;
  top.d_wstrb = 0xf;
  top.d_valid = 1;
  top.eval();
  int cycles = 0;
  while (!(top.i_ready && top.d_ready)) {
    tick(&top);
    check(++cycles <= kLatency, "parallel transfers arrived too late");
  }
  check(cycles == kLatency, "parallel transfer latency");
  tick(&top);
  top.i_valid = top.d_valid = 0;
  top.eval();

  r = transact_i(&top, kBase + 8, 0, 0);
  check(!r.error && r.data == 0x55667788u, "I-port parallel write committed");
  r = transact_d(&top, kBase + 12, 0, 0);
  check(!r.error && r.data == 0xaabbccddu, "D-port parallel write committed");

  r = transact_i(&top, kBase + 64, 0, 0);
  check(r.error && r.wait_cycles == kLatency, "out-of-range request reports delayed error");
  r = transact_d(&top, kBase + 2, 0, 0);
  check(r.error && r.wait_cycles == kLatency, "misaligned request reports delayed error");

  if (failures) {
    std::fprintf(stderr, "tb_axdram_model: %d FAILURE(S)\n", failures);
    return 1;
  }
  std::puts("tb_axdram_model: PASS");
  return 0;
}
