// Unit test for components/alu/single-cycle/alu.sv: directed edge cases plus random
// cross-check against an independent C model.
#include <cstdio>
#include <cstdint>
#include "Valu.h"
#include "verilated.h"

static int failures = 0;

// op = {funct7[5], funct3}
static uint32_t model(uint32_t a, uint32_t b, unsigned op) {
  const bool mod = op & 8;
  switch (op & 7) {
    case 0: return mod ? a - b : a + b;
    case 1: return a << (b & 31);
    case 2: return (int32_t)a < (int32_t)b;
    case 3: return a < b;
    case 4: return a ^ b;
    case 5: return mod ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31);
    case 6: return a | b;
    case 7: return a & b;
  }
  return 0;
}

static uint32_t xorshift(uint32_t& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return s;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Valu* alu = new Valu;

  auto run = [&](uint32_t a, uint32_t b, unsigned op) {
    alu->a = a; alu->b = b; alu->op = op; alu->eval();
    const uint32_t want = model(a, b, op);
    if (alu->y != want) {
      printf("FAIL op=%x a=%08x b=%08x: got %08x want %08x\n", op, a, b,
             alu->y, want);
      failures++;
    }
  };

  // directed edges
  const unsigned ops[] = {0x0, 0x8, 0x1, 0x2, 0x3, 0x4, 0x5, 0xD, 0x6, 0x7};
  const uint32_t vals[] = {0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
                           31, 32, 0xAAAAAAAA, 0x55555555};
  for (unsigned op : ops)
    for (uint32_t a : vals)
      for (uint32_t b : vals) run(a, b, op);

  // random cross-check
  uint32_t seed = 0xA70712C5;
  for (int i = 0; i < 100000; i++) {
    const uint32_t a = xorshift(seed), b = xorshift(seed);
    run(a, b, ops[xorshift(seed) % 10]);
  }

  delete alu;
  if (failures) { printf("tb_alu: %d FAILURES\n", failures); return 1; }
  printf("tb_alu: PASS\n");
  return 0;
}
