#include "role.h"

/* Generic role-header operations, shared by every role. */
static void role_ring_doorbell(void) { mmio_write32(AX_ROLE_DOORBELL, 1u); }

static void role_wait_done(void) {
  while (!(mmio_read32(AX_ROLE_STATUS) & AX_ROLE_STATUS_DONE)) {}
}

uint32_t role_discover(void) { return mmio_read32(AX_ROLE_ID); }

uint32_t role_version(void) { return mmio_read32(AX_ROLE_VERSION); }

const char *role_name(uint32_t role_id) {
  switch (role_id) {
    case AX_ROLE_ID_LOOPBACK: return "loopback";
    case AX_ROLE_ID_TPU:      return "tpu-lite";
    case AX_ROLE_ID_GPU:      return "gpu-compute";
    default:                  return "unknown";
  }
}

/* Drive one loopback copy through the full descriptor cycle — program the
 * descriptor, ring the doorbell, poll for completion, read the result back —
 * proving aXos owns the whole role protocol, not just discovery.  Loopback is
 * the one role driveable with no role-specific job encoding, so it is the
 * kernel's self-test; per-role job marshaling (GEMM, SIMT kernels) layers on
 * top of this same header driver. */
int role_loopback_selftest(void) {
  const uint32_t words = 4u, dst_word = 16u;
  for (uint32_t i = 0; i < words; ++i)
    mmio_write32(AX_ROLE_LOOP_BUF + 4u * i, 0xa5000000u | i);
  mmio_write32(AX_ROLE_LOOP_SRC, 0u);            /* byte offset of source */
  mmio_write32(AX_ROLE_LOOP_DST, 4u * dst_word); /* byte offset of destination */
  mmio_write32(AX_ROLE_LOOP_LEN, words);
  role_ring_doorbell();
  role_wait_done();
  for (uint32_t i = 0; i < words; ++i)
    if (mmio_read32(AX_ROLE_LOOP_BUF + 4u * (dst_word + i)) != (0xa5000000u | i))
      return -1;
  return 0;
}

/* Same descriptor cycle as the self-test, but the payload comes from the
 * caller (the host-link service) instead of a fixed pattern.  Source words
 * occupy [0, words); the destination is placed immediately after them. */
void role_loopback_copy(const uint32_t *in, uint32_t *out, uint32_t words) {
  for (uint32_t i = 0; i < words; ++i)
    mmio_write32(AX_ROLE_LOOP_BUF + 4u * i, in[i]);
  mmio_write32(AX_ROLE_LOOP_SRC, 0u);
  mmio_write32(AX_ROLE_LOOP_DST, 4u * words);
  mmio_write32(AX_ROLE_LOOP_LEN, words);
  role_ring_doorbell();
  role_wait_done();
  for (uint32_t i = 0; i < words; ++i)
    out[i] = mmio_read32(AX_ROLE_LOOP_BUF + 4u * (words + i));
}

/* Pack four consecutive int8 operands into one little-endian word, the layout
 * both the TPU weight tile and activation buffer expect. */
static uint32_t pack4(const int8_t *p) {
  return (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8) |
         ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24);
}

/* Run one TPU-lite GEMM: load the 8x8 weight tile and M activation rows, latch
 * CTRL/M, ring the doorbell, and read back the M x 8 int32 result tile. */
void role_tpu_gemm(const int8_t *w, const int8_t *a, uint32_t m,
                   uint32_t ctrl, int32_t *c_out) {
  for (uint32_t r = 0; r < 8u; ++r) {
    mmio_write32(AX_ROLE_TPU_W + 8u * r, pack4(&w[8u * r]));
    mmio_write32(AX_ROLE_TPU_W + 8u * r + 4u, pack4(&w[8u * r + 4u]));
  }
  for (uint32_t i = 0; i < m; ++i) {
    mmio_write32(AX_ROLE_TPU_A + 8u * i, pack4(&a[8u * i]));
    mmio_write32(AX_ROLE_TPU_A + 8u * i + 4u, pack4(&a[8u * i + 4u]));
  }
  mmio_write32(AX_ROLE_TPU_CTRL, ctrl);
  mmio_write32(AX_ROLE_TPU_M, m);
  role_ring_doorbell();
  role_wait_done();
  for (uint32_t i = 0; i < m * 8u; ++i)
    c_out[i] = (int32_t)mmio_read32(AX_ROLE_TPU_C + 4u * i);
}

/* Run one GPU-compute job: upload the kernel and the flat data buffer, launch
 * NTHREADS lanes over the program, and read the data buffer back. */
void role_gpu_run(const uint32_t *prog, uint32_t ninsn,
                  const uint32_t *data_in, uint32_t ndata,
                  uint32_t nthreads, uint32_t *data_out) {
  for (uint32_t i = 0; i < ninsn; ++i)
    mmio_write32(AX_ROLE_GPU_PROG + 4u * i, prog[i]);
  for (uint32_t i = 0; i < ndata; ++i)
    mmio_write32(AX_ROLE_GPU_DATA + 4u * i, data_in[i]);
  mmio_write32(AX_ROLE_GPU_NTHREADS, nthreads);
  mmio_write32(AX_ROLE_GPU_NINSN, ninsn);
  role_ring_doorbell();
  role_wait_done();
  for (uint32_t i = 0; i < ndata; ++i)
    data_out[i] = mmio_read32(AX_ROLE_GPU_DATA + 4u * i);
}
