#include <stdint.h>

#include "platform.h"

/* Sv32 PTE bits. The bootstrap installs A/D eagerly for its identity/device
 * mappings; page-table memory remains in the mapped kernel superpage. */
enum {
  PTE_V = 1u << 0,
  PTE_R = 1u << 1,
  PTE_W = 1u << 2,
  PTE_X = 1u << 3,
  PTE_A = 1u << 6,
  PTE_D = 1u << 7,
  MSTATUS_MPP = 3u << 11,
  MSTATUS_MPP_S = 1u << 11,
  SATP_MODE_SV32 = 1u << 31,
};

extern void s_entry(void);

/* Kept below the 128 KiB SoC RAM limit and aligned to their physical pages.
 * The root maps the three superpages needed by the first kernel; test_finisher
 * occupies one 4 KiB L0 mapping because it is not 4 MiB aligned. */
static volatile uint32_t root_pt[1024] __attribute__((aligned(4096)));
static volatile uint32_t low_pt[1024] __attribute__((aligned(4096)));

static uint32_t pte_leaf(uint32_t physical, uint32_t flags) {
  return ((physical >> 12) << 10) | flags;
}

static uint32_t pte_pointer(uint32_t physical) {
  return ((physical >> 12) << 10) | PTE_V;
}

static inline uint32_t csr_read_mstatus(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mstatus" : "=r"(value));
  return value;
}

static inline void csr_write_mstatus(uint32_t value) {
  __asm__ volatile("csrw mstatus, %0" :: "r"(value));
}

static inline void csr_write_mepc(uint32_t value) {
  __asm__ volatile("csrw mepc, %0" :: "r"(value));
}

static inline void csr_write_satp(uint32_t value) {
  __asm__ volatile("csrw satp, %0" :: "r"(value));
}

void m_setup(void) {
  const uint32_t kernel_flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
  const uint32_t device_flags = PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;

  /* Kernel RAM (VPN1 512), UART (64), and CLINT (8) are 4 MiB superpages. */
  root_pt[0x200] = pte_leaf(0x80000000u, kernel_flags);
  root_pt[0x040] = pte_leaf(AX_UART_BASE, device_flags);
  root_pt[0x008] = pte_leaf(AX_CLINT_BASE, device_flags);
  root_pt[0x000] = pte_pointer((uint32_t)(uintptr_t)low_pt);
  low_pt[(AX_TEST_BASE >> 12) & 0x3ffu] =
      pte_leaf(AX_TEST_BASE, device_flags);

  csr_write_satp(SATP_MODE_SV32 | ((uint32_t)(uintptr_t)root_pt >> 12));
  __asm__ volatile("sfence.vma zero, zero");
  csr_write_mepc((uint32_t)(uintptr_t)s_entry);
  csr_write_mstatus((csr_read_mstatus() & ~MSTATUS_MPP) | MSTATUS_MPP_S);
}

void kmain(void) {
  uart_puts("aXos: S-mode Sv32 online\n");
  test_finish(0);
}
