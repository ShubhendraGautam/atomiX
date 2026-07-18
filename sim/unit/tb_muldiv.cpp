// Unit test for the iterative RV32M unit.  The model spells out the ISA's
// divide-by-zero and signed-overflow rules independently of the RTL.
#include <cstdio>
#include <cstdint>
#include "Vmuldiv.h"
#include "verilated.h"

static uint32_t model(uint32_t a, uint32_t b, unsigned op) {
  const int64_t sa = (int32_t)a, sb = (int32_t)b;
  switch (op) {
    case 0: return (uint32_t)((uint64_t)a * b);
    case 1: return (uint32_t)((uint64_t)(sa * sb) >> 32);
    case 2: return (uint32_t)((uint64_t)(sa * (int64_t)(uint64_t)b) >> 32);
    case 3: return (uint32_t)(((uint64_t)a * b) >> 32);
    case 4: return b ? (uint32_t)(sa / sb) : 0xffffffffu;
    case 5: return b ? a / b : 0xffffffffu;
    case 6: return b ? (uint32_t)(sa % sb) : a;
    default: return b ? a % b : a;
  }
}

static uint32_t xorshift(uint32_t& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vmuldiv* dut = new Vmuldiv;
  int failures = 0;
  dut->clk = 0; dut->rst = 1; dut->start = 0; dut->eval();
  dut->clk = 1; dut->eval(); dut->clk = 0; dut->rst = 0; dut->eval();

  auto run = [&](uint32_t a, uint32_t b, unsigned op) {
    dut->a = a; dut->b = b; dut->op = op; dut->start = 1;
    dut->clk = 0; dut->eval();  // establish operands before the start edge
    dut->clk = 1; dut->eval(); dut->clk = 0; dut->start = 0; dut->eval();
    int cycles = 0;
    while (!dut->done && cycles++ < 40) {
      dut->clk = 1; dut->eval(); dut->clk = 0; dut->eval();
    }
    const uint32_t want = model(a, b, op);
    if (!dut->done || dut->result != want) {
      printf("FAIL op=%u a=%08x b=%08x cycles=%d got=%08x want=%08x\n",
             op, a, b, cycles, dut->result, want);
      ++failures;
    }
    // Consume the final done cycle before issuing the next operation.
    dut->clk = 1; dut->eval(); dut->clk = 0; dut->eval();
  };

  const uint32_t edges[] = {0, 1, 2, 0x7fffffff, 0x80000000,
                            0xffffffff, 0xaaaaaaaa, 0x55555555};
  for (unsigned op = 0; op < 8; ++op)
    for (uint32_t a : edges)
      for (uint32_t b : edges) run(a, b, op);

  uint32_t seed = 0x4d554c44;
  for (int i = 0; i < 20000; ++i)
    run(xorshift(seed), xorshift(seed), xorshift(seed) & 7);

  delete dut;
  if (failures) { printf("tb_muldiv: %d FAILURES\n", failures); return 1; }
  printf("tb_muldiv: PASS\n");
  return 0;
}
