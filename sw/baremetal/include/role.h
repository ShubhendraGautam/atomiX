#pragma once

#include "platform.h"

/* Shell + role contract (DESIGN.md §3.3): one fixed 64 KiB aXbus window.
 * Software discovers the role by reading ROLE_ID; zero means no role is
 * present.  Offsets past AX_ROLE_COUNT are defined by the selected role. */
#define AX_ROLE_BASE 0x40000000u

#define AX_ROLE_ID       (AX_ROLE_BASE + 0x0000u)
#define AX_ROLE_VERSION  (AX_ROLE_BASE + 0x0004u)
#define AX_ROLE_DOORBELL (AX_ROLE_BASE + 0x0008u)
#define AX_ROLE_STATUS   (AX_ROLE_BASE + 0x000cu)

#define AX_ROLE_STATUS_BUSY 0x1u
#define AX_ROLE_STATUS_DONE 0x2u

/* role.loopback programming model. */
#define AX_ROLE_LOOP_ID    0x4c4f4f50u /* "LOOP" */
#define AX_ROLE_LOOP_SRC   (AX_ROLE_BASE + 0x0010u)
#define AX_ROLE_LOOP_DST   (AX_ROLE_BASE + 0x0014u)
#define AX_ROLE_LOOP_LEN   (AX_ROLE_BASE + 0x0018u)
#define AX_ROLE_LOOP_COUNT (AX_ROLE_BASE + 0x001cu)
#define AX_ROLE_LOOP_BUF   (AX_ROLE_BASE + 0x1000u)

/* role.tpu-lite programming model: C = acc_base + A x W, int8 operands,
 * int32 results.  W is the stationary 8x8 tile; A is M x 8 (M <= 256);
 * rows pack one int8 per byte, little-endian, two words per row. */
#define AX_ROLE_TPU_ID    0x5450554cu /* "TPUL" */
#define AX_ROLE_TPU_CTRL  (AX_ROLE_BASE + 0x0010u)
#define AX_ROLE_TPU_M     (AX_ROLE_BASE + 0x0014u)
#define AX_ROLE_TPU_COUNT (AX_ROLE_BASE + 0x0018u)
#define AX_ROLE_TPU_W     (AX_ROLE_BASE + 0x0100u)
#define AX_ROLE_TPU_A     (AX_ROLE_BASE + 0x1000u)
#define AX_ROLE_TPU_C     (AX_ROLE_BASE + 0x2000u)

#define AX_ROLE_TPU_CTRL_RELU 0x1u
#define AX_ROLE_TPU_CTRL_ACC  0x2u

static inline uint32_t mmio_read32(uint32_t addr) {
  return *(volatile const uint32_t *)(uintptr_t)addr;
}

static inline uint32_t role_id(void) { return mmio_read32(AX_ROLE_ID); }

static inline void role_ring_doorbell(void) {
  mmio_write32(AX_ROLE_DOORBELL, 1u);
}

static inline void role_wait_done(void) {
  while (!(mmio_read32(AX_ROLE_STATUS) & AX_ROLE_STATUS_DONE)) {}
}
