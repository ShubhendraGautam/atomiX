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
  PTE_U = 1u << 4,
  PTE_A = 1u << 6,
  PTE_D = 1u << 7,
  MSTATUS_MPP = 3u << 11,
  MSTATUS_MPP_S = 1u << 11,
  SSTATUS_SPIE = 1u << 5,
  SATP_MODE_SV32 = 1u << 31,
  TASK_COUNT = 2,
  TASK_NONE = TASK_COUNT,
  TRAP_FRAME_BYTES = 128,
  TRAP_FRAME_A0 = 8,
  TRAP_FRAME_A7 = 15,
  TRAP_FRAME_SP = 30,
  SCAUSE_USER_ECALL = 8,
  SCAUSE_SUPERVISOR_SOFTWARE = 0x80000001u,
  SYS_USER_ONLINE = 1,
  SYS_CONSOLE_PUTC = 2,
  USER_CODE_VA = 0x40000000u,
  USER_STACK_VA = USER_CODE_VA + PAGE_SIZE,
};

extern void s_entry(void);
extern void machine_timer_trap(void);
extern void user_entry(void);

static volatile uint32_t supervisor_ticks;

struct task {
  uint32_t *trap_frame;
  uint32_t sepc;
  uint32_t sstatus;
  uint32_t satp;
  uint32_t *user_stack;
};

static struct task tasks[TASK_COUNT];
static uint32_t current_task = TASK_NONE;
static volatile uint32_t scheduler_running;
static volatile uint32_t scheduled_ticks;
static volatile uint32_t users_online;
static volatile uint32_t users_printed;

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

static inline uint32_t csr_read_sp(void) {
  uint32_t value;
  __asm__ volatile("mv %0, sp" : "=r"(value));
  return value;
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

static inline void csr_write_mscratch(uint32_t value) {
  __asm__ volatile("csrw mscratch, %0" :: "r"(value));
}

static inline void csr_write_mideleg(uint32_t value) {
  __asm__ volatile("csrw mideleg, %0" :: "r"(value));
}

static inline void csr_write_medeleg(uint32_t value) {
  __asm__ volatile("csrw medeleg, %0" :: "r"(value));
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
  csr_write_mscratch(csr_read_sp());
  csr_write_mtvec((uint32_t)(uintptr_t)machine_timer_trap);
  csr_write_medeleg(1u << SCAUSE_USER_ECALL);
  /* MTIP stays M-owned; the shim raises delegated SSIP for S-mode policy. */
  csr_write_mideleg(1u << 1);
  csr_write_mie((1u << 7) | (1u << 1));
  /* Keep interrupts out of the bootstrap; kmain arms the first scheduler tick
   * once its allocator, task state, and trap stacks are all ready. */
  clint_arm_timer(0x00100000u);
  csr_write_satp(SATP_MODE_SV32 | ((uint32_t)(uintptr_t)root_pt >> 12));
  __asm__ volatile("sfence.vma zero, zero");
  csr_write_mepc((uint32_t)(uintptr_t)s_entry);
  csr_write_mstatus((csr_read_mstatus() & ~MSTATUS_MPP) | MSTATUS_MPP_S);
}

static uint32_t *schedule(uint32_t *trap_frame) {
  if (!scheduler_running) return trap_frame;

  if (current_task != TASK_NONE) {
    tasks[current_task].trap_frame = trap_frame;
    tasks[current_task].sepc = csr_read_sepc();
    tasks[current_task].sstatus = csr_read_sstatus();
  }
  current_task = current_task == TASK_NONE ? 0u : current_task ^ 1u;
  csr_write_satp(tasks[current_task].satp);
  __asm__ volatile("sfence.vma zero, zero");
  csr_write_sepc(tasks[current_task].sepc);
  csr_write_sstatus(tasks[current_task].sstatus);
  return tasks[current_task].trap_frame;
}

uint32_t *supervisor_trap(uint32_t *trap_frame) {
  const uint32_t cause = csr_read_scause();

  if (cause == SCAUSE_SUPERVISOR_SOFTWARE) {
    /* M-mode turns MTIP into delegated SSIP for scheduler policy. */
    csr_write_sip(0);
    supervisor_ticks++;
    if (!scheduler_running) return trap_frame;
    scheduled_ticks++;
    if (users_online == 3u && users_printed == 3u && scheduled_ticks >= 4u)
      test_finish(0);
    return schedule(trap_frame);
  }

  if (cause == SCAUSE_USER_ECALL) {
    const uint32_t syscall = trap_frame[TRAP_FRAME_A7];
    if (syscall == SYS_USER_ONLINE) {
      const uint32_t id = trap_frame[TRAP_FRAME_A0];
      if (id >= TASK_COUNT || id != current_task ||
          *tasks[id].user_stack != (0x51a00000u | id))
        test_finish(1);
      users_online |= 1u << id;
    } else if (syscall == SYS_CONSOLE_PUTC) {
      if (current_task == TASK_NONE ||
          trap_frame[TRAP_FRAME_A0] != ('A' + current_task))
        test_finish(1);
      uart_putchar((char)trap_frame[TRAP_FRAME_A0]);
      users_printed |= 1u << current_task;
    } else {
      test_finish(1);
    }
    csr_write_sepc(csr_read_sepc() + 4u);
    return trap_frame;
  }

  test_finish(1);
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

static void zero_page(uint32_t *page) {
  for (uint32_t i = 0; i < PAGE_SIZE / sizeof(*page); ++i) page[i] = 0;
}

static void scheduler_make_user_task(uint32_t slot) {
  uint32_t *const page_root = page_alloc();
  uint32_t *const user_pt = page_alloc();
  uint32_t *const user_stack = page_alloc();
  uint32_t *const kernel_stack = page_alloc();
  if (page_root == 0 || user_pt == 0 || user_stack == 0 || kernel_stack == 0)
    test_finish(1);
  zero_page(page_root);
  zero_page(user_pt);
  for (uint32_t i = 0; i < 1024u; ++i) page_root[i] = root_pt[i];
  page_root[USER_CODE_VA >> 22] = pte_pointer((uint32_t)(uintptr_t)user_pt);
  user_pt[0] = pte_leaf(0x80000000u, PTE_V | PTE_R | PTE_X | PTE_U | PTE_A);
  user_pt[1] = pte_leaf((uint32_t)(uintptr_t)user_stack,
                        PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
  uint32_t *const frame =
      (uint32_t *)((uintptr_t)kernel_stack + PAGE_SIZE - TRAP_FRAME_BYTES);
  for (uint32_t i = 0; i < TRAP_FRAME_BYTES / sizeof(*frame); ++i) frame[i] = 0;
  tasks[slot].trap_frame = frame;
  tasks[slot].satp = SATP_MODE_SV32 | ((uint32_t)(uintptr_t)page_root >> 12);
  tasks[slot].user_stack = user_stack;
  tasks[slot].sepc = USER_CODE_VA +
      ((uint32_t)(uintptr_t)user_entry - 0x80000000u);
  frame[TRAP_FRAME_A0] = slot;
  frame[TRAP_FRAME_SP] = USER_STACK_VA + PAGE_SIZE;
  /* SRET returns to U mode with supervisor interrupts enabled. */
  tasks[slot].sstatus = SSTATUS_SPIE;
}

static void scheduler_start(void) {
  scheduler_make_user_task(0);
  scheduler_make_user_task(1);
  scheduler_running = 1;
  clint_arm_timer(2000);
}

void kmain(void) {
  page_init();
  page_allocator_self_test();
  uart_puts("aXos: Sv32 isolated U-mode scheduler online\n");
  scheduler_start();
  for (;;) __asm__ volatile("wfi");
}
