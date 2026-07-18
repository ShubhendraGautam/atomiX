// Unit smoke test for components/core/pipeline5/regfile.sv.
#include <cstdio>
#include <cstdlib>
#include "Vregfile.h"
#include "verilated.h"

static Vregfile* rf;
static int failures = 0;

static void tick() {
  rf->clk = 0; rf->eval();
  rf->clk = 1; rf->eval();
  rf->clk = 0; rf->eval();
}

static void check(const char* what, uint32_t got, uint32_t want) {
  if (got != want) {
    printf("FAIL %s: got %08x want %08x\n", what, got, want);
    failures++;
  }
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  rf = new Vregfile;

  // x0 reads as zero even after a write attempt
  rf->we = 1; rf->waddr = 0; rf->wdata = 0xDEADBEEF;
  tick();
  rf->we = 0; rf->raddr1 = 0; rf->raddr2 = 0; rf->eval();
  check("x0 read", rf->rdata1, 0);

  // plain write then read
  rf->we = 1; rf->waddr = 5; rf->wdata = 0x12345678;
  tick();
  rf->we = 0; rf->raddr1 = 5; rf->eval();
  check("write/read x5", rf->rdata1, 0x12345678);

  // both read ports independent
  rf->we = 1; rf->waddr = 17; rf->wdata = 0xCAFE0017;
  tick();
  rf->we = 0; rf->raddr1 = 5; rf->raddr2 = 17; rf->eval();
  check("port1 x5", rf->rdata1, 0x12345678);
  check("port2 x17", rf->rdata2, 0xCAFE0017);

  // write-before-read bypass: same-cycle visibility, before any clock edge
  rf->we = 1; rf->waddr = 9; rf->wdata = 0x0BADF00D;
  rf->raddr1 = 9; rf->raddr2 = 5; rf->eval();
  check("bypass x9", rf->rdata1, 0x0BADF00D);
  check("no-bypass x5", rf->rdata2, 0x12345678);
  tick();  // and it also lands in the array
  rf->we = 0; rf->eval();
  check("post-bypass x9", rf->rdata1, 0x0BADF00D);

  // bypass must not fire for x0
  rf->we = 1; rf->waddr = 0; rf->wdata = 0xFFFFFFFF;
  rf->raddr1 = 0; rf->eval();
  check("x0 bypass", rf->rdata1, 0);

  delete rf;
  if (failures) { printf("tb_regfile: %d FAILURES\n", failures); return 1; }
  printf("tb_regfile: PASS\n");
  return 0;
}
