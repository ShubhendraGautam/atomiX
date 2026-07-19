/* GPU-compute role proof against role.gpu-compute: discovery, kernel and data
 * upload, doorbell, completion polling, and result readback for three SIMT
 * kernels — saxpy, fused multiply+ReLU, and a gather (reverse) that exercises
 * computed per-lane addresses and the masked thread tail.  Each kernel's result
 * is checked against an on-core interpreter of the exact same instruction set,
 * so the RTL is verified against an independent software model of its ISA, not
 * against hand-computed constants.  The saxpy kernel is also timed against an
 * on-core saxpy loop for the DESIGN.md benchmark.  Runs on the RTL SoC only;
 * the ISS does not model the role window. */
#include "platform.h"
#include "role.h"

#define DATA_WORDS 4096
#define WINDOW     200      /* compared prefix of the flat data buffer */
#define N          50       /* threads; not a multiple of 8 -> exercises tail */
#define BASE_A     0
#define BASE_B     64
#define BASE_C     128

static int32_t hmem[DATA_WORDS];   /* host mirror of the role's global memory */
static uint32_t rng = 0x2468ace1u;

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
  uart_puts("role gpu-compute: FAIL ");
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

/* On-core interpreter of the gpu-compute ISA — the correctness oracle.  It
 * reproduces the RTL's SIMT semantics exactly: waves of 8 lanes in lockstep,
 * per-lane registers, a flat global buffer, ascending-lane store order within
 * an instruction, and tid >= nthreads predicated off. */
static void gpu_ref(const uint32_t *prog, int ninsn, int nthreads,
                    int32_t *mem) {
  int waves = (nthreads + 7) / 8;
  for (int w = 0; w < waves; ++w) {
    int32_t regs[8][8];
    for (int l = 0; l < 8; ++l)
      for (int r = 0; r < 8; ++r) regs[l][r] = 0;
    for (int pc = 0; pc < ninsn; ++pc) {
      uint32_t insn = prog[pc];
      uint32_t op = (insn >> 26) & 0x3fu;
      uint32_t rd = (insn >> 23) & 7u, ra = (insn >> 20) & 7u,
               rb = (insn >> 17) & 7u;
      int32_t imm = (int32_t)(insn << 15) >> 15;   /* sign-extend imm[16:0] */
      if (op == AX_GPU_HALT) break;
      for (int l = 0; l < 8; ++l) {
        int tid = w * 8 + l;
        int active = tid < nthreads;
        int32_t a = regs[l][ra], b = regs[l][rb];
        uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
        switch (op) {
          case AX_GPU_TID:  regs[l][rd] = tid; break;
          case AX_GPU_LI:   regs[l][rd] = imm; break;
          case AX_GPU_MOV:  regs[l][rd] = a; break;
          case AX_GPU_LDX:  regs[l][rd] = active ? mem[ua & (DATA_WORDS - 1)] : 0;
                            break;
          case AX_GPU_STX:  if (active) mem[ua & (DATA_WORDS - 1)] = b; break;
          case AX_GPU_ADD:  regs[l][rd] = a + b; break;
          case AX_GPU_SUB:  regs[l][rd] = a - b; break;
          case AX_GPU_MUL:  regs[l][rd] = a * b; break;
          case AX_GPU_AND:  regs[l][rd] = a & b; break;
          case AX_GPU_OR:   regs[l][rd] = a | b; break;
          case AX_GPU_XOR:  regs[l][rd] = a ^ b; break;
          case AX_GPU_SLL:  regs[l][rd] = (int32_t)(ua << (ub & 31)); break;
          case AX_GPU_SRL:  regs[l][rd] = (int32_t)(ua >> (ub & 31)); break;
          case AX_GPU_SRA:  regs[l][rd] = a >> (ub & 31); break;
          case AX_GPU_MIN:  regs[l][rd] = a < b ? a : b; break;
          case AX_GPU_MAX:  regs[l][rd] = a > b ? a : b; break;
          case AX_GPU_ADDI: regs[l][rd] = a + imm; break;
          case AX_GPU_MULI: regs[l][rd] = a * imm; break;
          default: break;
        }
      }
    }
  }
}

static void upload_prog(const uint32_t *prog, int n) {
  for (int i = 0; i < n; ++i)
    mmio_write32(AX_ROLE_GPU_PROG + 4u * (uint32_t)i, prog[i]);
}
static void upload_data(void) {
  for (int i = 0; i < WINDOW; ++i)
    mmio_write32(AX_ROLE_GPU_DATA + 4u * (uint32_t)i, (uint32_t)hmem[i]);
}

/* Fill the compared window: random A and B inputs, a sentinel in C so a stray
 * store into the masked tail would be caught. */
static void seed_data(void) {
  for (int i = 0; i < WINDOW; ++i) hmem[i] = (int32_t)0x7abc0000u | i;
  for (int i = 0; i < N; ++i) {
    hmem[BASE_A + i] = (int32_t)(rnd() & 0xffu) - 128;
    hmem[BASE_B + i] = (int32_t)(rnd() & 0xffu) - 128;
  }
}

/* Run a kernel on the role, returning the doorbell-to-done cycle count. */
static uint32_t run_gpu(const uint32_t *prog, int ninsn, int nthreads) {
  upload_prog(prog, ninsn);
  upload_data();
  mmio_write32(AX_ROLE_GPU_NTHREADS, (uint32_t)nthreads);
  mmio_write32(AX_ROLE_GPU_NINSN, (uint32_t)ninsn);
  uint32_t t0 = rdcycle();
  role_ring_doorbell();
  role_wait_done();
  return rdcycle() - t0;
}

/* Compare the role's data buffer against the host reference over the window. */
static void check_against_ref(const uint32_t *prog, int ninsn, unsigned code,
                              const char *what) {
  gpu_ref(prog, ninsn, N, hmem);
  for (int i = 0; i < WINDOW; ++i)
    if (mmio_read32(AX_ROLE_GPU_DATA + 4u * (uint32_t)i) != (uint32_t)hmem[i])
      fail(code, what);
}

static void clear_done(unsigned code) {
  mmio_write32(AX_ROLE_STATUS, AX_ROLE_STATUS_DONE);
  if (mmio_read32(AX_ROLE_STATUS) & AX_ROLE_STATUS_DONE)
    fail(code, "DONE did not clear");
}

int main(void) {
  if (role_id() != AX_ROLE_GPU_ID) fail(1, "discovery: ROLE_ID mismatch");
  if (mmio_read32(AX_ROLE_VERSION) == 0) fail(2, "VERSION reads zero");

  /* Kernel 1 — saxpy: C[tid] = 3*A[tid] + B[tid]. */
  const uint32_t saxpy[] = {
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),           /* r0 = tid              */
    gpu_insn(AX_GPU_LDX,  1, 0, 0, 0),           /* r1 = A[tid]           */
    gpu_insn(AX_GPU_ADDI, 2, 0, 0, BASE_B),      /* r2 = tid + BASE_B     */
    gpu_insn(AX_GPU_LDX,  3, 2, 0, 0),           /* r3 = B[tid]           */
    gpu_insn(AX_GPU_MULI, 1, 1, 0, 3),           /* r1 = 3*A[tid]         */
    gpu_insn(AX_GPU_ADD,  1, 1, 3, 0),           /* r1 = 3*A[tid]+B[tid]  */
    gpu_insn(AX_GPU_ADDI, 4, 0, 0, BASE_C),      /* r4 = tid + BASE_C     */
    gpu_insn(AX_GPU_STX,  0, 4, 1, 0),           /* C[tid] = r1           */
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  int saxpy_n = (int)(sizeof saxpy / sizeof saxpy[0]);
  seed_data();
  uint32_t gpu_cycles = run_gpu(saxpy, saxpy_n, N);
  check_against_ref(saxpy, saxpy_n, 3, "saxpy mismatch");
  if (mmio_read32(AX_ROLE_GPU_COUNT) != 1u) fail(4, "COUNT after saxpy");
  clear_done(5);

  /* Kernel 2 — fused multiply + ReLU: C[tid] = max(A[tid]*B[tid], 0). */
  const uint32_t frelu[] = {
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),
    gpu_insn(AX_GPU_LDX,  1, 0, 0, 0),           /* r1 = A[tid]           */
    gpu_insn(AX_GPU_ADDI, 2, 0, 0, BASE_B),
    gpu_insn(AX_GPU_LDX,  3, 2, 0, 0),           /* r3 = B[tid]           */
    gpu_insn(AX_GPU_MUL,  1, 1, 3, 0),           /* r1 = A[tid]*B[tid]    */
    gpu_insn(AX_GPU_LI,   5, 0, 0, 0),           /* r5 = 0                */
    gpu_insn(AX_GPU_MAX,  1, 1, 5, 0),           /* r1 = relu(r1)         */
    gpu_insn(AX_GPU_ADDI, 4, 0, 0, BASE_C),
    gpu_insn(AX_GPU_STX,  0, 4, 1, 0),
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  int frelu_n = (int)(sizeof frelu / sizeof frelu[0]);
  seed_data();
  (void)run_gpu(frelu, frelu_n, N);
  check_against_ref(frelu, frelu_n, 6, "multiply+relu mismatch");
  if (mmio_read32(AX_ROLE_GPU_COUNT) != 2u) fail(7, "COUNT after frelu");
  clear_done(8);

  /* Kernel 3 — gather (reverse): C[tid] = A[N-1-tid].  Computed per-lane load
   * addresses plus the masked tail (N is not a multiple of 8). */
  const uint32_t reverse[] = {
    gpu_insn(AX_GPU_LI,   6, 0, 0, N - 1),       /* r6 = N-1              */
    gpu_insn(AX_GPU_TID,  0, 0, 0, 0),           /* r0 = tid              */
    gpu_insn(AX_GPU_SUB,  1, 6, 0, 0),           /* r1 = N-1-tid          */
    gpu_insn(AX_GPU_LDX,  2, 1, 0, 0),           /* r2 = A[N-1-tid]       */
    gpu_insn(AX_GPU_ADDI, 3, 0, 0, BASE_C),
    gpu_insn(AX_GPU_STX,  0, 3, 2, 0),           /* C[tid] = r2           */
    gpu_insn(AX_GPU_HALT, 0, 0, 0, 0),
  };
  int reverse_n = (int)(sizeof reverse / sizeof reverse[0]);
  seed_data();
  (void)run_gpu(reverse, reverse_n, N);
  check_against_ref(reverse, reverse_n, 9, "gather/reverse mismatch");
  clear_done(10);

  /* NTHREADS = 0 completes immediately and leaves the buffer untouched. */
  seed_data();
  upload_data();
  mmio_write32(AX_ROLE_GPU_NTHREADS, 0u);
  mmio_write32(AX_ROLE_GPU_NINSN, (uint32_t)saxpy_n);
  upload_prog(saxpy, saxpy_n);
  role_ring_doorbell();
  role_wait_done();
  if (mmio_read32(AX_ROLE_GPU_COUNT) != 4u) fail(11, "COUNT after empty job");
  for (int i = 0; i < WINDOW; ++i)
    if (mmio_read32(AX_ROLE_GPU_DATA + 4u * (uint32_t)i) != (uint32_t)hmem[i])
      fail(12, "empty job disturbed the buffer");

  /* On-core saxpy over the same inputs, for the benchmark line. */
  seed_data();
  uint32_t cpu0 = rdcycle();
  for (int i = 0; i < N; ++i)
    hmem[BASE_C + i] = 3 * hmem[BASE_A + i] + hmem[BASE_B + i];
  uint32_t cpu_cycles = rdcycle() - cpu0;

  uart_puts("gpu saxpy cycles: 0x");
  puthex(gpu_cycles);
  uart_puts("\ncpu saxpy cycles: 0x");
  puthex(cpu_cycles);
  uart_puts("\nrole gpu-compute: PASS\n");
  test_finish(0);
}
