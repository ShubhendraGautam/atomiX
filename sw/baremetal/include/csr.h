#pragma once

#include <stdint.h>

static inline uint32_t csr_read_mcause(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mcause" : "=r"(value));
  return value;
}

static inline void csr_write_mtvec(uint32_t value) {
  __asm__ volatile("csrw mtvec, %0" :: "r"(value));
}

static inline void csr_set_mie(uint32_t mask) {
  __asm__ volatile("csrs mie, %0" :: "r"(mask));
}

static inline void csr_set_mstatus(uint32_t mask) {
  __asm__ volatile("csrs mstatus, %0" :: "r"(mask));
}
