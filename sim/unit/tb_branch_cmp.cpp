// Unit test for rtl/core/branch_cmp.sv: random cross-check vs C model.
#include <cstdio>
#include <cstdint>
#include "Vbranch_cmp.h"
#include "verilated.h"

static int model(uint32_t a, uint32_t b, unsigned f3) {
  switch (f3) {
    case 0: return a == b;
    case 1: return a != b;
    case 4: return (int32_t)a < (int32_t)b;
    case 5: return (int32_t)a >= (int32_t)b;
    case 6: return a < b;
    case 7: return a >= b;
  }
  return 0;
}

static uint32_t xorshift(uint32_t& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vbranch_cmp* dut = new Vbranch_cmp;
  const unsigned f3s[] = {0, 1, 4, 5, 6, 7};
  const uint32_t edges[] = {0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
  int failures = 0;

  auto run = [&](uint32_t a, uint32_t b, unsigned f3) {
    dut->a = a; dut->b = b; dut->f3 = f3; dut->eval();
    if (dut->taken != model(a, b, f3)) {
      printf("FAIL f3=%u a=%08x b=%08x: got %d want %d\n", f3, a, b,
             dut->taken, model(a, b, f3));
      failures++;
    }
  };

  for (unsigned f3 : f3s)
    for (uint32_t a : edges)
      for (uint32_t b : edges) run(a, b, f3);

  uint32_t seed = 0xB4A2C100;
  for (int i = 0; i < 100000; i++)
    run(xorshift(seed), xorshift(seed), f3s[xorshift(seed) % 6]);

  delete dut;
  if (failures) { printf("tb_branch_cmp: %d FAILURES\n", failures); return 1; }
  printf("tb_branch_cmp: PASS\n");
  return 0;
}
