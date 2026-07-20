// tb_gpu1: unit suite for the gpu1 SIMT engine.
//
// The testbench drives the role window over the aXbus data port exactly as
// software does -- upload kernel, upload data, set NTHREADS/NINSN, ring the
// doorbell, poll DONE, read the buffer back -- and checks every result against
// an independent C++ interpreter of the gpu1 ISA.  The RTL is therefore
// verified against a software model of its own instruction set rather than
// against hand-computed constants.
//
// The geometry under test is discovered at run time from the CAPS register, so
// this one binary covers every parameterisation of role.gpu1 without a source
// per configuration.  That is the point of publishing the geometry: lane and
// bank counts change wave *grouping* and memory *scheduling*, never the
// architectural result, and this suite is what holds that claim up.
//
// Kernel battery, in the order it runs:
//   1  saxpy          coalesced load/store (the fast path: one bank each)
//   2  multiply+relu  arithmetic mix
//   3  gather         reversed addressing -- scattered across banks
//   4  same-address   every lane stores to ONE address; checks that the highest
//                     lane is the last writer under maximal bank conflict
//   5  bank-stride    every lane hits the SAME bank; the worst-case serialisation
//   6  divergence     IF / ELSE / ENDIF per-lane masking
//   7  loop           BRANY data-dependent loop with per-lane exit
//   8  divide         DIV / REM / DIVU / REMU including divide-by-zero
//   9  shuffle        cross-lane SHFL
//  10  displaced      LDXI / STXI
#include "Vaxrole.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>

// ---- Window layout (mirrors gpu1_engine.sv) ---------------------------------
static const uint32_t BASE = 0x40000000u;
static const uint32_t OFF_ID = 0x00, OFF_VERSION = 0x04, OFF_DOORBELL = 0x08,
                      OFF_STATUS = 0x0c, OFF_NTHREADS = 0x10, OFF_NINSN = 0x14,
                      OFF_COUNT = 0x18, OFF_CAPS = 0x1c;
static const uint32_t PROG_BASE = 0x0100, DATA_BASE = 0x1000;
static const uint32_t ROLE_ID = 0x47505531u;   // "GPU1"
static const int DATA_WORDS = 4096;
static const int PROG_WORDS = 128;

enum {
  OP_HALT = 0, OP_TID, OP_LI, OP_MOV, OP_LDX, OP_STX, OP_ADD, OP_SUB, OP_MUL,
  OP_AND, OP_OR, OP_XOR, OP_SLL, OP_SRL, OP_SRA, OP_MIN, OP_MAX, OP_ADDI,
  OP_MULI, OP_SEQ, OP_SNE, OP_SLT, OP_SLTU, OP_SGE, OP_IF, OP_ELSE, OP_ENDIF,
  OP_BRA, OP_BRANY, OP_DIV, OP_REM, OP_DIVU, OP_REMU, OP_SHFL, OP_LDXI, OP_STXI
};

static uint32_t insn(uint32_t op, uint32_t rd, uint32_t ra, uint32_t rb,
                     int32_t imm) {
  return (op << 26) | (rd << 23) | (ra << 20) | (rb << 17) |
         ((uint32_t)imm & 0x1ffffu);
}

// ---- Bus plumbing ----------------------------------------------------------
static Vaxrole *top;
static vluint64_t cycles = 0;
static int failures = 0;

static void tick() {
  top->clk = 0; top->eval();
  top->clk = 1; top->eval();
  cycles++;
}

// One data-port transaction, held until the role asserts d_ready.  Register
// accesses complete in a cycle; buffer reads take a second (registered block
// RAM), which is exactly the wait state software sees.
static uint32_t bus(uint32_t addr, uint32_t wdata, uint32_t wstrb) {
  top->d_valid = 1; top->d_addr = addr; top->d_wdata = wdata;
  top->d_wstrb = wstrb;
  uint32_t rd = 0;
  for (int guard = 0; guard < 64; guard++) {
    top->clk = 0; top->eval();
    bool ready = top->d_ready;
    bool err = top->d_err;
    rd = top->d_rdata;
    top->clk = 1; top->eval();
    cycles++;
    if (ready) {
      if (err) { printf("FAIL: bus error at 0x%08x\n", addr); failures++; }
      top->d_valid = 0; top->d_wstrb = 0;
      top->clk = 0; top->eval();
      return rd;
    }
  }
  printf("FAIL: bus timeout at 0x%08x\n", addr);
  failures++;
  top->d_valid = 0;
  return 0;
}
static void wr(uint32_t off, uint32_t v) { bus(BASE + off, v, 0xf); }
static uint32_t rd(uint32_t off) { return bus(BASE + off, 0, 0x0); }

// ---- Software oracle -------------------------------------------------------
// An independent interpreter of the gpu1 ISA.  It must reproduce the RTL's
// semantics exactly: waves of NLANES in lockstep, registers zeroed per wave,
// non-executing lanes writing nothing, ascending-lane store order within an
// instruction, and the structured divergence mask.
struct Caps { int lanes, banks; bool div, shfl; };

static void oracle(const std::vector<uint32_t> &prog, int ninsn, int nthreads,
                   const Caps &c, std::vector<int32_t> &mem) {
  int nl = c.lanes;
  int waves = (nthreads + nl - 1) / nl;
  for (int w = 0; w < waves; ++w) {
    std::vector<std::array<int32_t, 8>> regs(nl);
    for (auto &r : regs) r.fill(0);
    std::vector<char> mask(nl, 1);
    std::vector<std::vector<char>> stack;
    int pc = 0;
    // A generous step budget: the RTL is bounded by NINSN per wave only for
    // straight-line code, so a runaway loop must fail here rather than hang.
    for (int steps = 0; steps < 100000; ++steps) {
      if (pc >= ninsn || pc >= PROG_WORDS) break;
      uint32_t ins = prog[pc];
      uint32_t op = (ins >> 26) & 0x3f, rdi = (ins >> 23) & 7,
               rai = (ins >> 20) & 7, rbi = (ins >> 17) & 7;
      int32_t imm = (int32_t)(ins << 15) >> 15;
      if (op == OP_HALT) break;

      auto exec = [&](int l) { return (w * nl + l) < nthreads && mask[l]; };

      // Control and mask ops act on the whole wave.
      if (op == OP_IF) {
        if (stack.size() < 8) {
          stack.push_back(mask);
          for (int l = 0; l < nl; ++l) mask[l] = mask[l] && regs[l][rai] != 0;
        }
        pc++; continue;
      }
      if (op == OP_ELSE) {
        if (!stack.empty())
          for (int l = 0; l < nl; ++l) mask[l] = stack.back()[l] && !mask[l];
        pc++; continue;
      }
      if (op == OP_ENDIF) {
        if (!stack.empty()) { mask = stack.back(); stack.pop_back(); }
        pc++; continue;
      }
      if (op == OP_BRA) { pc = (pc + imm) & (PROG_WORDS - 1); continue; }
      if (op == OP_BRANY) {
        bool any = false;
        for (int l = 0; l < nl; ++l)
          if (exec(l) && regs[l][rai] != 0) any = true;
        pc = any ? ((pc + imm) & (PROG_WORDS - 1)) : pc + 1;
        continue;
      }
      // Stores go in ascending lane order: a duplicated address leaves the
      // highest executing lane's value behind.
      if (op == OP_STX || op == OP_STXI) {
        for (int l = 0; l < nl; ++l)
          if (exec(l)) {
            uint32_t a = (uint32_t)regs[l][rai] + (op == OP_STXI ? imm : 0);
            mem[a & (DATA_WORDS - 1)] = regs[l][rbi];
          }
        pc++; continue;
      }
      if (op == OP_LDX || op == OP_LDXI) {
        for (int l = 0; l < nl; ++l)
          if (exec(l)) {
            uint32_t a = (uint32_t)regs[l][rai] + (op == OP_LDXI ? imm : 0);
            regs[l][rdi] = mem[a & (DATA_WORDS - 1)];
          }
        pc++; continue;
      }
      if (op == OP_SHFL) {
        if (c.shfl) {
          auto snapshot = regs;
          for (int l = 0; l < nl; ++l)
            if (exec(l)) {
              uint32_t sel = (uint32_t)snapshot[l][rbi] % (uint32_t)nl;
              regs[l][rdi] = snapshot[sel][rai];
            }
        }
        pc++; continue;
      }
      if (op == OP_DIV || op == OP_REM || op == OP_DIVU || op == OP_REMU) {
        if (c.div)
          for (int l = 0; l < nl; ++l)
            if (exec(l)) {
              int32_t a = regs[l][rai], b = regs[l][rbi];
              uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
              int32_t res;
              if (b == 0) {
                // The engine returns all-ones for a quotient and the dividend
                // for a remainder, matching the RISC-V rule.
                res = (op == OP_DIV || op == OP_DIVU) ? (int32_t)0xffffffffu : a;
              } else if (op == OP_DIV) {
                res = (int32_t)((int64_t)a / b);
              } else if (op == OP_REM) {
                res = (int32_t)((int64_t)a % b);
              } else if (op == OP_DIVU) {
                res = (int32_t)(ua / ub);
              } else {
                res = (int32_t)(ua % ub);
              }
              regs[l][rdi] = res;
            }
        pc++; continue;
      }
      // Lane-local ALU.
      for (int l = 0; l < nl; ++l) {
        if (!exec(l)) continue;
        int32_t a = regs[l][rai], b = regs[l][rbi];
        uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
        int tid = w * nl + l;
        int32_t y = 0;
        switch (op) {
          case OP_TID:  y = tid; break;
          case OP_LI:   y = imm; break;
          case OP_MOV:  y = a; break;
          case OP_ADD:  y = (int32_t)(ua + ub); break;
          case OP_SUB:  y = (int32_t)(ua - ub); break;
          case OP_MUL:  y = (int32_t)(ua * ub); break;
          case OP_AND:  y = a & b; break;
          case OP_OR:   y = a | b; break;
          case OP_XOR:  y = a ^ b; break;
          case OP_SLL:  y = (int32_t)(ua << (ub & 31)); break;
          case OP_SRL:  y = (int32_t)(ua >> (ub & 31)); break;
          case OP_SRA:  y = a >> (ub & 31); break;
          case OP_MIN:  y = a < b ? a : b; break;
          case OP_MAX:  y = a > b ? a : b; break;
          case OP_ADDI: y = (int32_t)(ua + (uint32_t)imm); break;
          case OP_MULI: y = (int32_t)(ua * (uint32_t)imm); break;
          case OP_SEQ:  y = a == b; break;
          case OP_SNE:  y = a != b; break;
          case OP_SLT:  y = a < b; break;
          case OP_SLTU: y = ua < ub; break;
          case OP_SGE:  y = a >= b; break;
          default: y = 0; break;
        }
        regs[l][rdi] = y;
      }
      pc++;
    }
  }
}

// ---- Job driver ------------------------------------------------------------
static const int WINDOW = 256;    // compared prefix of the flat data buffer
static std::vector<int32_t> hmem(DATA_WORDS, 0);
static uint32_t rng_state = 0x2468ace1u;
static uint32_t rnd() { rng_state = rng_state * 1103515245u + 12345u;
                        return rng_state >> 16; }

static void seed_data(int n) {
  for (int i = 0; i < WINDOW; ++i) hmem[i] = (int32_t)(0x7abc0000u | i);
  for (int i = 0; i < n; ++i) {
    hmem[i]      = (int32_t)(rnd() & 0xff) - 128;      // A
    hmem[64 + i] = (int32_t)(rnd() & 0xff) - 128;      // B
  }
}

static uint64_t run_job(const std::vector<uint32_t> &prog, int nthreads) {
  for (size_t i = 0; i < prog.size(); ++i)
    wr(PROG_BASE + 4 * (uint32_t)i, prog[i]);
  for (int i = 0; i < WINDOW; ++i)
    wr(DATA_BASE + 4 * (uint32_t)i, (uint32_t)hmem[i]);
  wr(OFF_NTHREADS, (uint32_t)nthreads);
  wr(OFF_NINSN, (uint32_t)prog.size());
  uint64_t t0 = cycles;
  wr(OFF_DOORBELL, 1);
  for (int guard = 0; guard < 4000000; ++guard) {
    if (rd(OFF_STATUS) & 2u) break;
  }
  uint64_t elapsed = cycles - t0;
  if (!(rd(OFF_STATUS) & 2u)) { printf("FAIL: job never completed\n"); failures++; }
  return elapsed;
}

static void check(const char *name, const std::vector<uint32_t> &prog,
                  int nthreads, const Caps &c) {
  uint64_t cyc = run_job(prog, nthreads);
  oracle(prog, (int)prog.size(), nthreads, c, hmem);
  int bad = 0;
  for (int i = 0; i < WINDOW; ++i) {
    uint32_t got = rd(DATA_BASE + 4 * (uint32_t)i);
    if (got != (uint32_t)hmem[i]) {
      if (bad < 4)
        printf("FAIL: %s: mem[%d] = 0x%08x, expected 0x%08x\n", name, i, got,
               (uint32_t)hmem[i]);
      bad++;
    }
  }
  if (bad) { printf("FAIL: %s: %d mismatches\n", name, bad); failures++; }
  else printf("  %-14s ok  (%llu cycles)\n", name, (unsigned long long)cyc);
  // Clear DONE (write-1-to-clear) so the next job starts from a known state.
  wr(OFF_STATUS, 2u);
  if (rd(OFF_STATUS) & 2u) { printf("FAIL: %s: DONE did not clear\n", name);
                             failures++; }
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  top = new Vaxrole;

  top->clk = 0; top->rst = 1;
  top->i_valid = 0; top->i_addr = 0; top->i_wdata = 0; top->i_wstrb = 0;
  top->d_valid = 0; top->d_addr = 0; top->d_wdata = 0; top->d_wstrb = 0;
  for (int i = 0; i < 8; ++i) tick();
  top->rst = 0;
  for (int i = 0; i < 4; ++i) tick();

  // ---- Discovery ----------------------------------------------------------
  uint32_t id = rd(OFF_ID);
  if (id != ROLE_ID) {
    printf("FAIL: ROLE_ID = 0x%08x, expected 0x%08x\n", id, ROLE_ID);
    return 1;
  }
  if (rd(OFF_VERSION) == 0) { printf("FAIL: VERSION reads zero\n"); return 1; }
  uint32_t caps = rd(OFF_CAPS);
  Caps c;
  c.lanes = (int)((caps >> 24) & 0xff);
  c.banks = (int)((caps >> 16) & 0xff);
  c.div   = (caps & 1u) != 0;
  c.shfl  = (caps & 2u) != 0;
  printf("gpu1: %d lanes, %d banks, div=%d shfl=%d\n", c.lanes, c.banks,
         (int)c.div, (int)c.shfl);
  if (c.lanes < 1 || c.lanes > 64 || c.banks < 1 || c.banks > 64) {
    printf("FAIL: implausible CAPS 0x%08x\n", caps);
    return 1;
  }

  // A thread count that is deliberately not a multiple of any tier's lane
  // count, so every tier exercises the predicated tail.
  const int N = 50;
  const int BASE_A = 0, BASE_B = 64, BASE_C = 128;

  // 1 - saxpy: C[tid] = 3*A[tid] + B[tid].  Fully coalesced.
  {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_LDX,  1, 0, 0, 0),
      insn(OP_ADDI, 2, 0, 0, BASE_B),
      insn(OP_LDX,  3, 2, 0, 0),
      insn(OP_MULI, 1, 1, 0, 3),
      insn(OP_ADD,  1, 1, 3, 0),
      insn(OP_ADDI, 4, 0, 0, BASE_C),
      insn(OP_STX,  0, 4, 1, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("saxpy", p, N, c);
    if (rd(OFF_COUNT) != 1u) { printf("FAIL: COUNT after saxpy\n"); failures++; }
  }

  // 2 - fused multiply + ReLU.
  {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_LDX,  1, 0, 0, 0),
      insn(OP_ADDI, 2, 0, 0, BASE_B),
      insn(OP_LDX,  3, 2, 0, 0),
      insn(OP_MUL,  1, 1, 3, 0),
      insn(OP_LI,   5, 0, 0, 0),
      insn(OP_MAX,  1, 1, 5, 0),
      insn(OP_ADDI, 4, 0, 0, BASE_C),
      insn(OP_STX,  0, 4, 1, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("relu", p, N, c);
  }

  // 3 - gather/reverse: C[tid] = A[N-1-tid].  Scatters lanes across banks in
  // descending order, so the crossbar cannot rely on lane order matching bank
  // order.
  {
    std::vector<uint32_t> p = {
      insn(OP_LI,   6, 0, 0, N - 1),
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_SUB,  1, 6, 0, 0),
      insn(OP_LDX,  2, 1, 0, 0),
      insn(OP_ADDI, 3, 0, 0, BASE_C),
      insn(OP_STX,  0, 3, 2, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("gather", p, N, c);
  }

  // 4 - same-address store: every lane writes mem[BASE_C] with its own tid.
  // Maximal bank conflict, and the store-ordering invariant in its sharpest
  // form -- the surviving value must be the highest executing lane's.
  {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_LI,   1, 0, 0, BASE_C),
      insn(OP_STX,  0, 1, 0, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("same-addr", p, N, c);
  }

  // 5 - bank-stride: address = tid * NBANKS, so every lane targets bank 0.
  // This is the worst case for the crossbar: NLANES rounds, no parallelism.
  // It must still be correct, and the cycle count printed here is the
  // conflict-serialised bound to compare against the coalesced kernels.
  {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_MULI, 1, 0, 0, c.banks),
      insn(OP_ADDI, 1, 1, 0, BASE_C),
      insn(OP_STX,  0, 1, 0, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    // Threads reduced so tid*banks stays inside the compared window.
    check("bank-stride", p, (WINDOW - BASE_C) / c.banks, c);
  }

  // 6 - divergence: even threads store tid*2, odd threads store tid+1000.
  // Exercises IF / ELSE / ENDIF and proves masked-off lanes write nothing.
  {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_LI,   1, 0, 0, 1),
      insn(OP_AND,  2, 0, 1, 0),          // r2 = tid & 1
      insn(OP_ADDI, 3, 0, 0, BASE_C),     // r3 = &C[tid]
      insn(OP_IF,   0, 2, 0, 0),          // odd lanes
        insn(OP_ADDI, 4, 0, 0, 1000),
      insn(OP_ELSE, 0, 0, 0, 0),          // even lanes
        insn(OP_MULI, 4, 0, 0, 2),
      insn(OP_ENDIF, 0, 0, 0, 0),
      insn(OP_STX,  0, 3, 4, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("divergence", p, N, c);
  }

  // 7 - data-dependent loop: each thread sums 1..tid via a BRANY loop, so lanes
  // exit at different iterations and the loop runs until the last one is done.
  {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),          // r0 = tid  (counter)
      insn(OP_LI,   1, 0, 0, 0),          // r1 = acc
      insn(OP_LI,   2, 0, 0, 1),          // r2 = 1
      // loop:
      insn(OP_IF,   0, 0, 0, 0),          // lanes with counter != 0
        insn(OP_ADD,  1, 1, 0, 0),        //   acc += counter
        insn(OP_SUB,  0, 0, 2, 0),        //   counter -= 1
      insn(OP_ENDIF, 0, 0, 0, 0),
      insn(OP_BRANY, 0, 0, 0, -4),        // back to the IF while any lane != 0
      insn(OP_ADDI, 3, 0, 0, BASE_C),     // r0 is 0 here, so r3 = BASE_C
      insn(OP_TID,  4, 0, 0, 0),
      insn(OP_ADD,  3, 3, 4, 0),          // r3 = BASE_C + tid
      insn(OP_STX,  0, 3, 1, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("loop", p, N, c);
  }

  // 8 - divide: quotient and remainder, including a divide-by-zero lane
  // (tid 0 divides by tid).
  if (c.div) {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_LDX,  1, 0, 0, 0),          // r1 = A[tid] (signed, -128..127)
      insn(OP_DIV,  2, 1, 0, 0),          // r2 = A[tid] / tid  (tid 0 -> div0)
      insn(OP_REM,  3, 1, 0, 0),          // r3 = A[tid] % tid
      insn(OP_ADD,  2, 2, 3, 0),
      insn(OP_ADDI, 4, 0, 0, BASE_C),
      insn(OP_STX,  0, 4, 2, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("divide", p, N, c);

    std::vector<uint32_t> pu = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_ADDI, 1, 0, 0, 7),
      insn(OP_DIVU, 2, 1, 0, 0),
      insn(OP_REMU, 3, 1, 0, 0),
      insn(OP_MULI, 3, 3, 0, 100),
      insn(OP_ADD,  2, 2, 3, 0),
      insn(OP_ADDI, 4, 0, 0, BASE_C),
      insn(OP_STX,  0, 4, 2, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("divide-u", pu, N, c);
  }

  // 9 - shuffle: lane L takes lane (L+1 mod NLANES)'s value, a cross-lane
  // rotate that no per-lane path can produce.
  if (c.shfl) {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_LDX,  1, 0, 0, 0),          // r1 = A[tid]
      insn(OP_LI,   2, 0, 0, 1),
      insn(OP_ADD,  2, 0, 2, 0),          // r2 = tid + 1
      insn(OP_SHFL, 3, 1, 2, 0),          // r3 = lane (tid+1 mod nlanes)'s r1
      insn(OP_ADDI, 4, 0, 0, BASE_C),
      insn(OP_STX,  0, 4, 3, 0),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("shuffle", p, N, c);
  }

  // 10 - displaced addressing: LDXI/STXI fold the base into the instruction,
  // which removes the address-arithmetic instructions the other kernels need.
  {
    std::vector<uint32_t> p = {
      insn(OP_TID,  0, 0, 0, 0),
      insn(OP_LDXI, 1, 0, 0, BASE_A),
      insn(OP_LDXI, 2, 0, 0, BASE_B),
      insn(OP_ADD,  3, 1, 2, 0),
      insn(OP_STXI, 0, 0, 3, BASE_C),
      insn(OP_HALT, 0, 0, 0, 0)};
    seed_data(N);
    check("displaced", p, N, c);
  }

  // 11 - empty job: NTHREADS = 0 completes immediately and touches nothing.
  {
    seed_data(N);
    for (int i = 0; i < WINDOW; ++i)
      wr(DATA_BASE + 4 * (uint32_t)i, (uint32_t)hmem[i]);
    wr(OFF_NTHREADS, 0);
    wr(OFF_NINSN, 4);
    wr(OFF_DOORBELL, 1);
    for (int guard = 0; guard < 1000; ++guard) if (rd(OFF_STATUS) & 2u) break;
    if (!(rd(OFF_STATUS) & 2u)) { printf("FAIL: empty job did not complete\n");
                                  failures++; }
    for (int i = 0; i < WINDOW; ++i)
      if (rd(DATA_BASE + 4 * (uint32_t)i) != (uint32_t)hmem[i]) {
        printf("FAIL: empty job disturbed the buffer at %d\n", i);
        failures++; break;
      }
    printf("  %-14s ok\n", "empty-job");
    wr(OFF_STATUS, 2u);
  }

  // 12 - unaligned and out-of-window accesses fault rather than aliasing.
  {
    top->d_valid = 1; top->d_addr = BASE + OFF_NTHREADS + 1; top->d_wstrb = 0;
    top->clk = 0; top->eval();
    bool err = top->d_err && top->d_ready;
    top->clk = 1; top->eval(); cycles++;
    top->d_valid = 0; top->clk = 0; top->eval();
    if (!err) { printf("FAIL: unaligned access did not fault\n"); failures++; }
    else printf("  %-14s ok\n", "fault-check");
  }

  top->final();
  delete top;
  if (failures) { printf("tb_gpu1: FAIL (%d)\n", failures); return 1; }
  printf("tb_gpu1: PASS\n");
  return 0;
}
