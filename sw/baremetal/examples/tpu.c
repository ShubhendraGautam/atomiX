/* TPU-lite role proof against role.tpu-lite: discovery, weight/activation
 * loading, doorbell, completion polling, and result readback for three GEMM
 * jobs — plain, accumulating (the K > 8 tiling primitive), and ReLU — each
 * checked against a software reference computed on the core.  The same
 * reference matmul doubles as the DESIGN.md benchmark: the test prints the
 * cycle counts for the offloaded and the CPU matmul.  Runs on the RTL SoC
 * only; the ISS does not model the role window. */
#include "platform.h"
#include "role.h"

#define TPU_M 12

static int8_t a_mat[TPU_M][8];
static int8_t w_mat[8][8];
static int32_t ref[TPU_M][8];
static uint32_t rng = 0x1234567u;

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
  uart_puts("role tpu-lite: FAIL ");
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

static uint32_t pack4(const int8_t *p) {
  return (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8) |
         ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24);
}

static void randomize_and_load(void) {
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c) w_mat[r][c] = (int8_t)rnd();
  for (int m = 0; m < TPU_M; ++m)
    for (int k = 0; k < 8; ++k) a_mat[m][k] = (int8_t)rnd();
  for (int r = 0; r < 8; ++r) {
    mmio_write32(AX_ROLE_TPU_W + 8u * (uint32_t)r, pack4(&w_mat[r][0]));
    mmio_write32(AX_ROLE_TPU_W + 8u * (uint32_t)r + 4u, pack4(&w_mat[r][4]));
  }
  for (int m = 0; m < TPU_M; ++m) {
    mmio_write32(AX_ROLE_TPU_A + 8u * (uint32_t)m, pack4(&a_mat[m][0]));
    mmio_write32(AX_ROLE_TPU_A + 8u * (uint32_t)m + 4u, pack4(&a_mat[m][4]));
  }
}

/* The software reference implements exactly the role's job semantics. */
static void reference(int acc, int relu) {
  for (int m = 0; m < TPU_M; ++m)
    for (int c = 0; c < 8; ++c) {
      int32_t s = acc ? ref[m][c] : 0;
      for (int r = 0; r < 8; ++r)
        s += (int32_t)a_mat[m][r] * (int32_t)w_mat[r][c];
      if (relu && s < 0) s = 0;
      ref[m][c] = s;
    }
}

static uint32_t run_job(uint32_t ctrl) {
  mmio_write32(AX_ROLE_TPU_CTRL, ctrl);
  mmio_write32(AX_ROLE_TPU_M, TPU_M);
  uint32_t t0 = rdcycle();
  role_ring_doorbell();
  role_wait_done();
  return rdcycle() - t0;
}

static void check_result(unsigned code, const char *what) {
  for (int m = 0; m < TPU_M; ++m)
    for (int c = 0; c < 8; ++c) {
      uint32_t word = AX_ROLE_TPU_C + 4u * (8u * (uint32_t)m + (uint32_t)c);
      if (mmio_read32(word) != (uint32_t)ref[m][c]) fail(code, what);
    }
}

int main(void) {
  if (role_id() != AX_ROLE_TPU_ID) fail(1, "discovery: ROLE_ID mismatch");
  if (mmio_read32(AX_ROLE_VERSION) == 0) fail(2, "VERSION reads zero");

  /* Job 1: plain GEMM, timed on both engines. */
  randomize_and_load();
  uint32_t cpu0 = rdcycle();
  reference(0, 0);
  uint32_t cpu_cycles = rdcycle() - cpu0;
  uint32_t tpu_cycles = run_job(0);
  check_result(3, "plain GEMM mismatch");
  if (mmio_read32(AX_ROLE_TPU_COUNT) != 1u) fail(4, "COUNT after first job");

  /* Clear DONE (write-1-to-clear) before reprogramming. */
  mmio_write32(AX_ROLE_STATUS, AX_ROLE_STATUS_DONE);
  if (mmio_read32(AX_ROLE_STATUS) & AX_ROLE_STATUS_DONE)
    fail(5, "DONE did not clear");

  /* Job 2: accumulate a second weight/activation tile into C — the K > 8
   * tiling primitive. */
  randomize_and_load();
  reference(1, 0);
  (void)run_job(AX_ROLE_TPU_CTRL_ACC);
  check_result(6, "accumulate mismatch");
  if (mmio_read32(AX_ROLE_TPU_COUNT) != 2u) fail(7, "COUNT after second job");

  /* Job 3: fresh GEMM with the ReLU output stage. */
  randomize_and_load();
  reference(0, 1);
  (void)run_job(AX_ROLE_TPU_CTRL_RELU);
  check_result(8, "relu mismatch");

  /* M = 0 completes immediately and leaves C untouched. */
  mmio_write32(AX_ROLE_TPU_M, 0u);
  role_ring_doorbell();
  role_wait_done();
  if (mmio_read32(AX_ROLE_TPU_COUNT) != 4u) fail(9, "COUNT after empty job");
  check_result(10, "empty job disturbed C");

  uart_puts("tpu gemm cycles: 0x");
  puthex(tpu_cycles);
  uart_puts("\ncpu gemm cycles: 0x");
  puthex(cpu_cycles);
  uart_puts("\nrole tpu-lite: PASS\n");
  test_finish(0);
}
