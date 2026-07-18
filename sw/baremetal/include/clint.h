#pragma once

#include <stdint.h>

#include "platform.h"

#define CLINT_MTIMECMP  (AX_CLINT_BASE + 0x4000u)
#define CLINT_MTIMECMPH (AX_CLINT_BASE + 0x4004u)
#define CLINT_MTIME     (AX_CLINT_BASE + 0xbff8u)

static inline uint32_t clint_mtime_low(void) {
  return *(volatile const uint32_t *)(uintptr_t)CLINT_MTIME;
}

static inline void clint_schedule_after(uint32_t delta) {
  const uint32_t deadline = clint_mtime_low() + delta;
  // Standard safe update order: make the comparator unreachable while its
  // low half changes, then install a zero high half for the current epoch.
  mmio_write32(CLINT_MTIMECMPH, 0xffffffffu);
  mmio_write32(CLINT_MTIMECMP, deadline);
  mmio_write32(CLINT_MTIMECMPH, 0);
}
