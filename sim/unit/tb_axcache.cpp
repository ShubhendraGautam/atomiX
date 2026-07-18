// Directed cache test: verify a miss fills a line, a hit avoids the delayed
// backing memory, writes stay write-through, and MMIO-style addresses bypass.
#include <cstdint>
#include <cstdio>

#include "Vaxcache_test_top.h"
#include "verilated.h"

struct response { uint32_t data; bool error; int memory_completions; };
static int failures = 0;
static constexpr uint32_t kBase = 0x80000000u;

static void check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    failures++;
  }
}

static void tick(Vaxcache_test_top* top, int* completions) {
  top->clk = 0;
  top->eval();
  if (top->mem_valid && top->mem_ready) ++*completions;
  top->clk = 1;
  top->eval();
  top->clk = 0;
  top->eval();
}

static response transact(Vaxcache_test_top* top, uint32_t address,
                         uint32_t data, uint8_t strobe) {
  top->c_addr = address;
  top->c_wdata = data;
  top->c_wstrb = strobe;
  top->c_valid = 1;
  top->eval();
  int completions = 0;
  int guard = 0;
  while (!top->c_ready) {
    tick(top, &completions);
    check(++guard < 100, "cache response timed out");
  }
  const response result = {top->c_rdata, bool(top->c_err), completions};
  tick(top, &completions);  // Complete the upper aXbus transaction.
  top->c_valid = 0;
  top->eval();
  return result;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vaxcache_test_top top;
  top.clk = 0;
  top.rst = 1;
  top.flush = 0;
  top.c_valid = 0;
  top.c_addr = top.c_wdata = 0;
  top.c_wstrb = 0;
  int ignored = 0;
  tick(&top, &ignored);
  top.rst = 0;
  top.eval();

  response r = transact(&top, kBase, 0, 0);
  check(!r.error && r.data == 0 && r.memory_completions == 4,
        "read miss fills exactly one four-word line");
  r = transact(&top, kBase + 4, 0, 0);
  check(!r.error && r.data == 0 && r.memory_completions == 0,
        "read hit avoids external memory");

  top.flush = 1;
  tick(&top, &ignored);
  top.flush = 0;
  top.eval();
  r = transact(&top, kBase + 4, 0, 0);
  check(!r.error && r.memory_completions == 4, "flush invalidates the line");

  r = transact(&top, kBase, 0x11223344u, 0xf);
  check(!r.error && r.memory_completions == 1, "write is forwarded once");
  r = transact(&top, kBase, 0, 0);
  check(!r.error && r.data == 0x11223344u && r.memory_completions == 4,
        "write invalidates then read refills updated line");
  r = transact(&top, kBase, 0x0000aa00u, 0x2);
  check(!r.error && r.memory_completions == 1, "byte write is forwarded once");
  r = transact(&top, kBase, 0, 0);
  check(!r.error && r.data == 0x1122aa44u, "byte write reaches refilled cache line");

  r = transact(&top, kBase + 128, 0, 0);
  check(!r.error && r.memory_completions == 1, "non-cacheable address bypasses");
  r = transact(&top, kBase + 256, 0, 0);
  check(r.error && r.memory_completions == 1, "downstream error propagates through bypass");

  if (failures) {
    std::fprintf(stderr, "tb_axcache: %d FAILURE(S)\n", failures);
    return 1;
  }
  std::puts("tb_axcache: PASS");
  return 0;
}
