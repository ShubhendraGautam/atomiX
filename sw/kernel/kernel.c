#include <stdint.h>

#include "page.h"
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
  SSTATUS_SPIE = 1u << 5,
  SSTATUS_SPP = 1u << 8,
  SATP_MODE_SV32 = 1u << 31,
  TASK_COUNT = 2,
  TASK_NONE = TASK_COUNT,
  TRAP_FRAME_BYTES = 128,
};

extern void s_entry(void);
extern void machine_timer_trap(void);

static volatile uint32_t supervisor_ticks;

struct task {
  uint32_t *trap_frame;
  uint32_t sepc;
  uint32_t sstatus;
};

static struct task tasks[TASK_COUNT];
static uint32_t current_task = TASK_NONE;
static volatile uint32_t scheduler_running;
static volatile uint32_t task_a_runs;
static volatile uint32_t task_b_runs;

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

static inline void csr_write_mtvec(uint32_t value) {
  __asm__ volatile("csrw mtvec, %0" :: "r"(value));
}

static inline void csr_write_mideleg(uint32_t value) {
  __asm__ volatile("csrw mideleg, %0" :: "r"(value));
}

static inline void csr_write_mie(uint32_t value) {
  __asm__ volatile("csrw mie, %0" :: "r"(value));
}

static inline uint32_t csr_read_scause(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, scause" : "=r"(value));
  return value;
}

static inline uint32_t csr_read_sepc(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, sepc" : "=r"(value));
  return value;
}

static inline void csr_write_sepc(uint32_t value) {
  __asm__ volatile("csrw sepc, %0" :: "r"(value));
}

static inline uint32_t csr_read_sstatus(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, sstatus" : "=r"(value));
  return value;
}

static inline void csr_write_sstatus(uint32_t value) {
  __asm__ volatile("csrw sstatus, %0" :: "r"(value));
}

static inline void csr_write_sip(uint32_t value) {
  __asm__ volatile("csrw sip, %0" :: "r"(value));
}

static void clint_arm_timer(uint32_t delta) {
  volatile uint32_t *const mtimecmp =
      (volatile uint32_t *)(uintptr_t)(AX_CLINT_BASE + 0x4000u);
  volatile const uint32_t *const mtime =
      (volatile const uint32_t *)(uintptr_t)(AX_CLINT_BASE + 0xbff8u);
  mtimecmp[1] = 0xffffffffu;
  mtimecmp[0] = *mtime + delta;
  mtimecmp[1] = 0;
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

  csr_write_mtvec((uint32_t)(uintptr_t)machine_timer_trap);
  /* MTIP stays M-owned; the shim raises delegated SSIP for S-mode policy. */
  csr_write_mideleg(1u << 1);
  csr_write_mie((1u << 7) | (1u << 1));
  clint_arm_timer(2000);
  csr_write_satp(SATP_MODE_SV32 | ((uint32_t)(uintptr_t)root_pt >> 12));
  __asm__ volatile("sfence.vma zero, zero");
  csr_write_mepc((uint32_t)(uintptr_t)s_entry);
  csr_write_mstatus((csr_read_mstatus() & ~MSTATUS_MPP) | MSTATUS_MPP_S);
}

uint32_t *supervisor_trap(uint32_t *trap_frame) {
  /* M-mode turns the hardware MTIP into this delegated supervisor SSIP. */
  if (csr_read_scause() != 0x80000001u) test_finish(1);
  csr_write_sip(0);
  supervisor_ticks++;

  if (!scheduler_running) return trap_frame;

  if (current_task != TASK_NONE) {
    tasks[current_task].trap_frame = trap_frame;
    tasks[current_task].sepc = csr_read_sepc();
    tasks[current_task].sstatus = csr_read_sstatus();
  }
  current_task = current_task == TASK_NONE ? 0u : current_task ^ 1u;
  csr_write_sepc(tasks[current_task].sepc);
  csr_write_sstatus(tasks[current_task].sstatus);
  return tasks[current_task].trap_frame;
}

static void page_allocator_self_test(void) {
  void *pages[32];
  const uint32_t total = page_free_count();

  if (total == 0 || total > (sizeof(pages) / sizeof(pages[0]))) test_finish(1);
  for (uint32_t i = 0; i < total; ++i) {
    pages[i] = page_alloc();
    if (pages[i] == 0 || ((uintptr_t)pages[i] & (PAGE_SIZE - 1u)))
      test_finish(1);
    *(volatile uint32_t *)pages[i] = 0xa50a0000u | i;
    if (*(volatile uint32_t *)pages[i] != (0xa50a0000u | i)) test_finish(1);
  }
  if (page_alloc() != 0 || page_free_count() != 0) test_finish(1);
  while (total != page_free_count()) page_free(pages[page_free_count()]);
}

static void task_pause_until_next_tick(void) {
  const uint32_t seen = supervisor_ticks;
  while (supervisor_ticks == seen) __asm__ volatile("nop");
}

static void task_a(void) {
  for (;;) {
    task_a_runs++;
    task_pause_until_next_tick();
  }
}

static void task_b(void) {
  for (;;) {
    task_b_runs++;
    if (task_a_runs >= 3u && task_b_runs >= 3u) test_finish(0);
    task_pause_until_next_tick();
  }
}

static void scheduler_make_task(uint32_t slot, void (*entry)(void)) {
  uint32_t *const stack = page_alloc();
  if (stack == 0) test_finish(1);
  uint32_t *const frame =
      (uint32_t *)((uintptr_t)stack + PAGE_SIZE - TRAP_FRAME_BYTES);
  for (uint32_t i = 0; i < TRAP_FRAME_BYTES / sizeof(*frame); ++i) frame[i] = 0;
  tasks[slot].trap_frame = frame;
  tasks[slot].sepc = (uint32_t)(uintptr_t)entry;
  /* SRET returns to an S-mode task with supervisor interrupts enabled. */
  tasks[slot].sstatus = SSTATUS_SPP | SSTATUS_SPIE;
}

static void scheduler_start(void) {
  scheduler_make_task(0, task_a);
  scheduler_make_task(1, task_b);
  scheduler_running = 1;
}

void kmain(void) {
  while (supervisor_ticks == 0) __asm__ volatile("nop");
  page_init();
  page_allocator_self_test();
  uart_puts("aXos: S-mode Sv32 preemptive scheduler online\n");
  scheduler_start();
  for (;;) __asm__ volatile("wfi");
}
