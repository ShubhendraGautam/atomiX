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
