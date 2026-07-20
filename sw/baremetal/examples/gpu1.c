/* gpu1 role proof: discovery through CAPS, kernel and data upload, doorbell,
 * completion polling, and result readback -- checked against an on-core
 * interpreter of the gpu1 ISA.
 *
 * The point of this program, as opposed to the unit testbench that covers the
 * same ISA, is that it runs on the real SoC: a CPU on the aXbus drives the role
 * through the shell window, so the driver path, the bus, the wait states on the
 * buffer reads, and the doorbell/status handshake are all in the loop.
 *
 * It is written against CAPS rather than a fixed lane count, so the same binary
 * is the suite for every parameterisation of role.gpu1 and for whichever core
 * the profile pairs it with.  Kernels that need an optional ISA group (divide,
 * shuffle) are skipped when CAPS says the build does not have it.
 *
 * Runs on the RTL SoC only; the ISS does not model the role window. */
#include "platform.h"
#include "role.h"

#define DATA_WORDS 4096
#define WINDOW     200      /* compared prefix of the flat data buffer */
#define N          50       /* threads; not a multiple of any tier's lane count */
#define BASE_A     0
#define BASE_B     64
#define BASE_C     128
#define MAXLANES   32

static int32_t hmem[DATA_WORDS];   /* host mirror of the role's global memory */
static uint32_t rng = 0x2468ace1u;
static int g_lanes, g_banks;
static int g_has_div, g_has_shfl;

static uint32_t rnd(void) {
  rng = rng * 1103515245u + 12345u;
  return rng >> 16;
}

static uint32_t rdcycle(void) {
  uint32_t v;
  __asm__ volatile("csrr %0, mcycle" : "=r"(v));
  return v;
}

static void fail(unsigned code, const char *what) {
  uart_puts("role gpu1: FAIL ");
  uart_puts(what);
  uart_puts("\n");
  test_finish(code);
}

static void puthex(uint32_t v) {
  for (int i = 28; i >= 0; i -= 4) {
    unsigned d = (v >> i) & 0xfu;
    uart_putchar((char)(d < 10 ? '0' + d : 'a' + (d - 10)));
  }
}

/* On-core interpreter of the gpu1 ISA -- the correctness oracle.  It reproduces
 * the RTL exactly: waves of g_lanes in lockstep, registers zeroed per wave, a
 * lane executing only when it is within the thread count AND enabled by the
 * divergence mask, ascending-lane store order, and a flat global buffer. */
static void gpu1_ref(const uint32_t *prog, int ninsn, int nthreads,
                     int32_t *mem) {
  int nl = g_lanes;
  int waves = (nthreads + nl - 1) / nl;
  for (int w = 0; w < waves; ++w) {
    int32_t regs[MAXLANES][8];
    unsigned char mask[MAXLANES];
    unsigned char stack[8][MAXLANES];
    int sp = 0;
    for (int l = 0; l < nl; ++l) {
      for (int r = 0; r < 8; ++r) regs[l][r] = 0;
      mask[l] = 1;
    }
    int pc = 0;
    for (int steps = 0; steps < 200000 && pc < ninsn; ++steps) {
      uint32_t insn = prog[pc];
      uint32_t op = (insn >> 26) & 0x3fu;
      uint32_t rd = (insn >> 23) & 7u, ra = (insn >> 20) & 7u,
               rb = (insn >> 17) & 7u;
      int32_t imm = (int32_t)(insn << 15) >> 15;
      if (op == AX_GPU_HALT) break;

      if (op == AX_GPU_IF) {
        if (sp < 8) {
          for (int l = 0; l < nl; ++l) stack[sp][l] = mask[l];
          sp++;
          for (int l = 0; l < nl; ++l)
            mask[l] = (unsigned char)(mask[l] && regs[l][ra] != 0);
        }
        pc++; continue;
      }
      if (op == AX_GPU_ELSE) {
        if (sp > 0)
          for (int l = 0; l < nl; ++l)
            mask[l] = (unsigned char)(stack[sp - 1][l] && !mask[l]);
        pc++; continue;
      }
      if (op == AX_GPU_ENDIF) {
        if (sp > 0) { sp--; for (int l = 0; l < nl; ++l) mask[l] = stack[sp][l]; }
        pc++; continue;
      }
      if (op == AX_GPU_BRA) { pc = (pc + imm) & 127; continue; }
      if (op == AX_GPU_BRANY) {
        int any = 0;
        for (int l = 0; l < nl; ++l)
          if ((w * nl + l) < nthreads && mask[l] && regs[l][ra] != 0) any = 1;
        pc = any ? ((pc + imm) & 127) : pc + 1;
        continue;
      }

      for (int l = 0; l < nl; ++l) {
        int tid = w * nl + l;
        if (tid >= nthreads || !mask[l]) continue;
        int32_t a = regs[l][ra], b = regs[l][rb];
        uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
        switch (op) {
          case AX_GPU_TID:  regs[l][rd] = tid; break;
          case AX_GPU_LI:   regs[l][rd] = imm; break;
          case AX_GPU_MOV:  regs[l][rd] = a; break;
          case AX_GPU_LDX:  regs[l][rd] = mem[ua & (DATA_WORDS - 1)]; break;
          case AX_GPU_LDXI: regs[l][rd] = mem[(ua + (uint32_t)imm) &
                                              (DATA_WORDS - 1)]; break;
          case AX_GPU_STX:  mem[ua & (DATA_WORDS - 1)] = b; break;
          case AX_GPU_STXI: mem[(ua + (uint32_t)imm) & (DATA_WORDS - 1)] = b; break;
          case AX_GPU_ADD:  regs[l][rd] = (int32_t)(ua + ub); break;
          case AX_GPU_SUB:  regs[l][rd] = (int32_t)(ua - ub); break;
          case AX_GPU_MUL:  regs[l][rd] = (int32_t)(ua * ub); break;
          case AX_GPU_AND:  regs[l][rd] = a & b; break;
          case AX_GPU_OR:   regs[l][rd] = a | b; break;
          case AX_GPU_XOR:  regs[l][rd] = a ^ b; break;
          case AX_GPU_SLL:  regs[l][rd] = (int32_t)(ua << (ub & 31)); break;
          case AX_GPU_SRL:  regs[l][rd] = (int32_t)(ua >> (ub & 31)); break;
          case AX_GPU_SRA:  regs[l][rd] = a >> (ub & 31); break;
          case AX_GPU_MIN:  regs[l][rd] = a < b ? a : b; break;
          case AX_GPU_MAX:  regs[l][rd] = a > b ? a : b; break;
          case AX_GPU_ADDI: regs[l][rd] = (int32_t)(ua + (uint32_t)imm); break;
          case AX_GPU_MULI: regs[l][rd] = (int32_t)(ua * (uint32_t)imm); break;
          case AX_GPU_SEQ:  regs[l][rd] = a == b; break;
          case AX_GPU_SNE:  regs[l][rd] = a != b; break;
          case AX_GPU_SLT:  regs[l][rd] = a < b; break;
          case AX_GPU_SLTU: regs[l][rd] = ua < ub; break;
          case AX_GPU_SGE:  regs[l][rd] = a >= b; break;
          /* RISC-V divide-by-zero rule: quotient all ones, remainder dividend. */
          case AX_GPU_DIV:  regs[l][rd] = b == 0 ? -1 : a / b; break;
          case AX_GPU_REM:  regs[l][rd] = b == 0 ? a : a % b; break;
          case AX_GPU_DIVU: regs[l][rd] = b == 0 ? (int32_t)0xffffffffu
                                                 : (int32_t)(ua / ub); break;
          case AX_GPU_REMU: regs[l][rd] = b == 0 ? a : (int32_t)(ua % ub); break;
          default: break;
        }
      }
      /* SHFL reads other lanes, so it needs the pre-instruction snapshot and
       * cannot be folded into the per-lane loop above. */
      if (op == AX_GPU_SHFL) {
        int32_t snap[MAXLANES];
        for (int l = 0; l < nl; ++l) snap[l] = regs[l][ra];
        for (int l = 0; l < nl; ++l) {
          int tid = w * nl + l;
          if (tid >= nthreads || !mask[l]) continue;
          regs[l][rd] = snap[(uint32_t)regs[l][rb] % (uint32_t)nl];
        }
      }
      pc++;
    }
  }
}

static void upload_prog(const uint32_t *prog, int n) {
  for (int i = 0; i < n; ++i)
    mmio_write32(AX_ROLE_GPU1_PROG + 4u * (uint32_t)i, prog[i]);
}
static void upload_data(void) {
  for (int i = 0; i < WINDOW; ++i)
    mmio_write32(AX_ROLE_GPU1_DATA + 4u * (uint32_t)i, (uint32_t)hmem[i]);
}

static void seed_data(void) {
  for (int i = 0; i < WINDOW; ++i) hmem[i] = (int32_t)(0x7abc0000u | (uint32_t)i);
  for (int i = 0; i < N; ++i) {
    hmem[BASE_A + i] = (int32_t)(rnd() & 0xffu) - 128;
    hmem[BASE_B + i] = (int32_t)(rnd() & 0xffu) - 128;
  }
}

static uint32_t run_gpu(const uint32_t *prog, int ninsn, int nthreads) {
  upload_prog(prog, ninsn);
  upload_data();
  mmio_write32(AX_ROLE_GPU1_NTHREADS, (uint32_t)nthreads);
  mmio_write32(AX_ROLE_GPU1_NINSN, (uint32_t)ninsn);
  uint32_t t0 = rdcycle();
  role_ring_doorbell();
  role_wait_done();
  return rdcycle() - t0;
}

static uint32_t check(const uint32_t *prog, int ninsn, int nthreads,
                      unsigned code, const char *what) {
  uint32_t cyc = run_gpu(prog, ninsn, nthreads);
  gpu1_ref(prog, ninsn, nthreads, hmem);
  for (int i = 0; i < WINDOW; ++i)
    if (mmio_read32(AX_ROLE_GPU1_DATA + 4u * (uint32_t)i) != (uint32_t)hmem[i])
      fail(code, what);
  mmio_write32(AX_ROLE_STATUS, AX_ROLE_STATUS_DONE);
  if (mmio_read32(AX_ROLE_STATUS) & AX_ROLE_STATUS_DONE)
    fail(code, "DONE did not clear");
  return cyc;
}

int main(void) {
  if (role_id() != AX_ROLE_GPU1_ID) fail(1, "discovery: ROLE_ID mismatch");
  if (mmio_read32(AX_ROLE_VERSION) == 0) fail(2, "VERSION reads zero");

  uint32_t caps = mmio_read32(AX_ROLE_GPU1_CAPS);
  g_lanes    = (int)AX_GPU1_CAPS_LANES(caps);
  g_banks    = (int)AX_GPU1_CAPS_BANKS(caps);
  g_has_div  = (caps & AX_GPU1_CAPS_DIV) != 0;
  g_has_shfl = (caps & AX_GPU1_CAPS_SHFL) != 0;
  if (g_lanes < 1 || g_lanes > MAXLANES) fail(3, "CAPS lane count implausible");
  if (g_banks < 1 || g_banks > MAXLANES) fail(3, "CAPS bank count implausible");

  uart_puts("gpu1 lanes: 0x");
  puthex((uint32_t)g_lanes);
  uart_puts(" banks: 0x");
  puthex((uint32_t)g_banks);
  uart_puts("\n");

  /* 1 - saxpy: C[tid] = 3*A[tid] + B[tid].  Fully coalesced: lane L touches
   * base+L, so every lane lands in a different bank and the whole wave's
   * load or store retires in one round. */
  const uint32_t saxpy[] = {
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
    gpu_insn(AX_GPU_LDXI, 1, 0, 0, BASE_A),
    gpu_insn(AX_GPU_LDXI, 3, 0, 0, BASE_B),
    gpu_insn(AX_GPU_MULI, 1, 1, 0, 3),
    gpu_insn(AX_GPU_ADD,  1, 1, 3, 0),
    gpu_insn(AX_GPU_STXI, 0, 0, 1, BASE_C),
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  int saxpy_n = (int)(sizeof saxpy / sizeof saxpy[0]);
  seed_data();
  uint32_t gpu_cycles = check(saxpy, saxpy_n, N, 4, "saxpy mismatch");
  if (mmio_read32(AX_ROLE_GPU1_COUNT) != 1u) fail(5, "COUNT after saxpy");

  /* 2 - gather/reverse: C[tid] = A[N-1-tid].  Descending addresses scatter the
   * lanes across banks in an order unrelated to lane index. */
  const uint32_t reverse[] = {
    gpu_insn(AX_GPU_LI,   6, 0, 0, N - 1),
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
    gpu_insn(AX_GPU_SUB,  1, 6, 0, 0),
    gpu_insn(AX_GPU_LDX,  2, 1, 0, 0),
    gpu_insn(AX_GPU_STXI, 0, 0, 2, BASE_C),
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  seed_data();
  (void)check(reverse, (int)(sizeof reverse / sizeof reverse[0]), N, 6,
              "gather mismatch");

  /* 3 - same-address store: every lane writes one address.  Maximal bank
   * conflict, and the store-ordering rule at its sharpest: the value left
   * behind must be the highest executing lane's. */
  const uint32_t samestore[] = {
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
    gpu_insn(AX_GPU_LI,   1, 0, 0, BASE_C),
    gpu_insn(AX_GPU_STX,  0, 1, 0, 0),
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  seed_data();
  (void)check(samestore, (int)(sizeof samestore / sizeof samestore[0]), N, 7,
              "same-address store ordering");

  /* 4 - divergence: odd threads take one arm, even threads the other. */
  const uint32_t diverge[] = {
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
    gpu_insn(AX_GPU_LI,   1, 0, 0, 1),
    gpu_insn(AX_GPU_AND,  2, 0, 1, 0),
    gpu_insn(AX_GPU_IF,   0, 2, 0, 0),
    gpu_insn(AX_GPU_ADDI, 4, 0, 0, 1000),
    gpu_insn(AX_GPU_ELSE, 0, 0, 0, 0),
    gpu_insn(AX_GPU_MULI, 4, 0, 0, 2),
    gpu_insn(AX_GPU_ENDIF, 0, 0, 0, 0),
    gpu_insn(AX_GPU_STXI, 0, 0, 4, BASE_C),
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  seed_data();
  (void)check(diverge, (int)(sizeof diverge / sizeof diverge[0]), N, 8,
              "divergence mismatch");

  /* 5 - data-dependent loop: thread tid sums 1..tid, so lanes leave the loop at
   * different iterations and BRANY keeps it alive until the last one exits. */
  const uint32_t loop[] = {
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
    gpu_insn(AX_GPU_LI,   1, 0, 0, 0),
    gpu_insn(AX_GPU_LI,   2, 0, 0, 1),
    gpu_insn(AX_GPU_IF,   0, 0, 0, 0),
    gpu_insn(AX_GPU_ADD,  1, 1, 0, 0),
    gpu_insn(AX_GPU_SUB,  0, 0, 2, 0),
    gpu_insn(AX_GPU_ENDIF, 0, 0, 0, 0),
    gpu_insn(AX_GPU_BRANY, 0, 0, 0, -4),
    gpu_insn(AX_GPU_TID,  4, 0, 0, 0),
    gpu_insn(AX_GPU_STXI, 0, 4, 1, BASE_C),
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  seed_data();
  (void)check(loop, (int)(sizeof loop / sizeof loop[0]), N, 9, "loop mismatch");

  /* 6 - divide, including a divide-by-zero lane (thread 0 divides by tid). */
  if (g_has_div) {
    const uint32_t divk[] = {
      gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
      gpu_insn(AX_GPU_LDXI, 1, 0, 0, BASE_A),
      gpu_insn(AX_GPU_DIV,  2, 1, 0, 0),
      gpu_insn(AX_GPU_REM,  3, 1, 0, 0),
      gpu_insn(AX_GPU_ADD,  2, 2, 3, 0),
      gpu_insn(AX_GPU_STXI, 0, 0, 2, BASE_C),
      gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
    };
    seed_data();
    (void)check(divk, (int)(sizeof divk / sizeof divk[0]), N, 10,
                "divide mismatch");
  }

  /* 7 - cross-lane shuffle: a rotate by one lane, which no per-lane datapath
   * can produce on its own. */
  if (g_has_shfl) {
    const uint32_t shfl[] = {
      gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
      gpu_insn(AX_GPU_LDXI, 1, 0, 0, BASE_A),
      gpu_insn(AX_GPU_LI,   2, 0, 0, 1),
      gpu_insn(AX_GPU_ADD,  2, 0, 2, 0),
      gpu_insn(AX_GPU_SHFL, 3, 1, 2, 0),
      gpu_insn(AX_GPU_STXI, 0, 0, 3, BASE_C),
      gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
    };
    seed_data();
    (void)check(shfl, (int)(sizeof shfl / sizeof shfl[0]), N, 11,
                "shuffle mismatch");
  }

  /* 8 - empty job: NTHREADS = 0 completes immediately and touches nothing. */
  seed_data();
  upload_data();
  mmio_write32(AX_ROLE_GPU1_NTHREADS, 0u);
  mmio_write32(AX_ROLE_GPU1_NINSN, (uint32_t)saxpy_n);
  upload_prog(saxpy, saxpy_n);
  role_ring_doorbell();
  role_wait_done();
  for (int i = 0; i < WINDOW; ++i)
    if (mmio_read32(AX_ROLE_GPU1_DATA + 4u * (uint32_t)i) != (uint32_t)hmem[i])
      fail(12, "empty job disturbed the buffer");

  /* On-core saxpy over the same inputs, for the benchmark line. */
  seed_data();
  uint32_t cpu0 = rdcycle();
  for (int i = 0; i < N; ++i)
    hmem[BASE_C + i] = 3 * hmem[BASE_A + i] + hmem[BASE_B + i];
  uint32_t cpu_cycles = rdcycle() - cpu0;

  uart_puts("gpu1 saxpy cycles: 0x");
  puthex(gpu_cycles);
  uart_puts("\ncpu  saxpy cycles: 0x");
  puthex(cpu_cycles);
  uart_puts("\nrole gpu1: PASS\n");
  test_finish(0);
}
